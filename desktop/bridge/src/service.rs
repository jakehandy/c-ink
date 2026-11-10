//! The bridge: runs Claude on a schedule, keeps the latest digest, serves it
//! to devices on the LAN (with mDNS so they find it), and mirrors it to the
//! optional relay. The desktop app and the headless `cink-bridge` binary both
//! run exactly this.

use std::collections::HashMap;
use std::fs;
use std::sync::Arc;
use std::time::Duration;

use anyhow::{anyhow, Result};
use chrono::{Local, TimeZone, Timelike};
use parking_lot::{Mutex, RwLock};
use serde::Serialize;

use crate::settings::{Device, Settings};
use crate::{claude, digest, prompt, seal};

#[derive(Clone, Debug, Default, Serialize)]
pub struct Status {
    pub running: bool,
    pub generated: i64,
    pub cards: usize,
    pub headline: String,
    pub last_error: Option<String>,
    pub last_duration_s: u64,
    pub next_run: i64,
    pub dropped_private: usize,
    /// When Claude last did a smart run (the morning plan or a check-in).
    pub smart_at: i64,
    /// "morning", "check-in" or "quick" for the last run.
    pub last_pass: String,
    /// The digest was identical to the last one, so devices got a 304 and redrew nothing.
    pub unchanged: bool,
    pub serving: bool,
    pub port: u16,
    pub host: String,
    pub last_seen: HashMap<String, i64>,
    pub relay_ok: Option<bool>,
}

/// What the last smart run left behind, kept on disk so a restart mid-day
/// does not redo the morning plan.
#[derive(Clone, Default, serde::Serialize, serde::Deserialize)]
struct Smart {
    /// Unix time of the run.
    at: i64,
    /// Local day it planned, "2026-11-10".
    day: String,
    plan: Vec<String>,
    /// Claude's answer, before built-in cards and the quick pass.
    raw: serde_json::Value,
}

#[derive(Default)]
struct State {
    status: Status,
    /// Claude's last good answer, reused with fresh built-in cards when a run fails.
    last_raw: Option<serde_json::Value>,
    smart: Option<Smart>,
    /// Weather card and when it was fetched; forecasts barely move in 30 minutes.
    weather: Option<(i64, String, serde_json::Value)>,
    /// Cloudflare Traffic card and when it was built (analytics lag a few minutes anyway).
    cf_traffic: Option<(i64, String, Option<serde_json::Value>)>,
    wire: Option<Vec<u8>>,
    etag: String,
    sealed: HashMap<String, (String, Vec<u8>)>, // device id -> (etag, blob)
}

#[derive(Clone)]
pub struct Bridge {
    pub settings: Arc<RwLock<Settings>>,
    state: Arc<Mutex<State>>,
    run_lock: Arc<Mutex<()>>,
}

fn now() -> i64 {
    Local::now().timestamp()
}

fn last_path() -> std::path::PathBuf {
    Settings::dir().join("last_digest.json")
}

fn smart_path() -> std::path::PathBuf {
    Settings::dir().join("smart.json")
}

/// Which pass a run should be.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Due {
    Morning,
    CheckIn,
    Quick,
}

/// The morning plan once a day (the first run after 4 AM), a check-in every
/// `smart_every_min`, and quick passes in between.
pub fn due(s: &Settings, last_smart_at: i64, last_smart_day: &str, now: chrono::DateTime<Local>, force: bool) -> Due {
    let today = now.format("%Y-%m-%d").to_string();
    let new_day = last_smart_day != today && now.hour() >= 4;
    if last_smart_at == 0 || new_day {
        return Due::Morning;
    }
    if force || (s.smart_every_min > 0 && now.timestamp() - last_smart_at >= s.smart_every_min as i64 * 60 - 60) {
        return Due::CheckIn;
    }
    Due::Quick
}

impl Bridge {
    pub fn new(settings: Settings) -> Bridge {
        let b = Bridge {
            settings: Arc::new(RwLock::new(settings)),
            state: Arc::new(Mutex::new(State::default())),
            run_lock: Arc::new(Mutex::new(())),
        };
        // Serve the last digest straight away after a restart.
        if let Ok(wire) = fs::read(last_path()) {
            if let Ok(v) = serde_json::from_slice::<serde_json::Value>(&wire) {
                let mut st = b.state.lock();
                st.status.generated = v["gen"].as_i64().unwrap_or(0);
                st.status.cards = v["cards"].as_array().map(|a| a.len()).unwrap_or(0);
                st.status.headline = v["hl"].as_str().unwrap_or("").to_string();
                st.etag = seal::etag(&wire);
                st.wire = Some(wire);
            }
        }
        if let Some(sm) = fs::read(smart_path()).ok().and_then(|b| serde_json::from_slice::<Smart>(&b).ok()) {
            let mut st = b.state.lock();
            st.status.smart_at = sm.at;
            st.last_raw = Some(sm.raw.clone());
            st.smart = Some(sm);
        }
        b
    }

    pub fn status(&self) -> Status {
        let mut s = self.state.lock().status.clone();
        s.host = host_name();
        s.port = self.settings.read().port;
        s
    }

    pub fn wire(&self) -> Option<Vec<u8>> {
        self.state.lock().wire.clone()
    }

    pub fn update_settings(&self, s: Settings) -> Result<()> {
        s.save()?;
        let refresh_changed = s.refresh_min != self.settings.read().refresh_min;
        *self.settings.write() = s;
        self.state.lock().sealed.clear(); // keys or devices may have changed
        if refresh_changed {
            self.state.lock().status.next_run = 0;
        }
        Ok(())
    }

    fn in_quiet(s: &Settings) -> bool {
        match s.quiet {
            Some((a, b)) if a != b => {
                let h = Local::now().hour() as u8;
                if a < b { h >= a && h < b } else { h >= a || h < b }
            }
            _ => false,
        }
    }

    /// Re-checks which connectors (read-only tools) Claude Code can see.
    pub fn refresh_tools(&self) -> Result<Vec<String>> {
        let path = self.settings.read().claude_path.clone();
        let tools = claude::discover_tools(&path)?;
        let mut s = self.settings.write();
        s.tools = tools.clone();
        s.tools_checked = now();
        let _ = s.save();
        Ok(tools)
    }

    /// A run asked for by the owner ("Run now"): always a smart pass.
    pub fn run_now(&self) -> Result<Status> {
        self.run(true)
    }

    /// One digest run. Safe to call from any thread; concurrent calls wait.
    pub fn run(&self, force_smart: bool) -> Result<Status> {
        let _g = self.run_lock.lock();
        self.state.lock().status.running = true;
        let started = std::time::Instant::now();
        let result = self.run_inner(force_smart);
        let mut st = self.state.lock();
        st.status.running = false;
        st.status.last_duration_s = started.elapsed().as_secs();
        st.status.next_run = now() + self.settings.read().refresh_min as i64 * 60;
        match result {
            Ok(()) => st.status.last_error = None,
            Err(e) => {
                log::warn!("digest run failed: {e:#}");
                st.status.last_error = Some(format!("{e:#}"));
            }
        }
        Ok(st.status.clone())
    }

    fn run_inner(&self, force_smart: bool) -> Result<()> {
        let stale = {
            let s = self.settings.read();
            s.tools.is_empty() || now() - s.tools_checked > 24 * 3600
        };
        if stale {
            if let Err(e) = self.refresh_tools() {
                log::warn!("tool discovery failed: {e:#}");
            }
        }
        let s = self.settings.read().clone();
        let now_l = Local::now();
        let extra = self.builtin_cards(&s, now_l);
        let smart = self.state.lock().smart.clone().unwrap_or_default();
        let pass = due(&s, smart.at, &smart.day, now_l, force_smart);
        let (mut raw, err) = match pass {
            Due::Morning | Due::CheckIn => {
                let has_github = s.tools.iter().any(|t| t.to_lowercase().contains("github"));
                let gh = if s.enabled("review") && !has_github { claude::gh_context() } else { None };
                let current: Vec<serde_json::Value> =
                    smart.raw["cards"].as_array().cloned().unwrap_or_default();
                let since = Local.timestamp_opt(smart.at, 0).single().unwrap_or(now_l);
                let kind = match pass {
                    Due::Morning => prompt::Pass::Morning,
                    _ => prompt::Pass::CheckIn { plan: &smart.plan, current: &current, since },
                };
                let p = prompt::build_pass(&s, now_l, &s.tools, gh.as_deref(), &extra, &kind);
                match claude::run_digest(&s.claude_path, &s.model, &p, prompt::SCHEMA, &s.tools) {
                    Ok(r) => {
                        let sm = Smart {
                            at: now_l.timestamp(),
                            day: now_l.format("%Y-%m-%d").to_string(),
                            plan: r["plan"].as_array().map(|a| a.iter().filter_map(|l| l.as_str().map(str::to_string)).collect())
                                .unwrap_or_default(),
                            raw: r.clone(),
                        };
                        let _ = fs::create_dir_all(Settings::dir());
                        let _ = fs::write(smart_path(), serde_json::to_vec(&sm).unwrap_or_default());
                        let mut st = self.state.lock();
                        st.last_raw = Some(r.clone());
                        st.status.smart_at = sm.at;
                        st.smart = Some(sm);
                        (r, None)
                    }
                    Err(e) => match self.state.lock().last_raw.clone() {
                        Some(r) => (r, Some(e)),
                        None if !extra.is_empty() => (serde_json::json!({"headline": "", "cards": []}), Some(e)),
                        None => return Err(e),
                    },
                }
            }
            Due::Quick => {
                let mut r = self.state.lock().last_raw.clone().unwrap_or_else(|| serde_json::json!({"headline": "", "cards": []}));
                let cal = prompt::calendar_tools(&s.tools);
                let mut err = None;
                if s.quick_calendar && !cal.is_empty() && (s.enabled("event") || s.enabled("commute")) {
                    match claude::run_digest(&s.claude_path, "haiku", &prompt::quick(now_l, &s.private_terms), prompt::QUICK_SCHEMA, &cal) {
                        Ok(q) => merge_events(&mut r, &q, now_l),
                        Err(e) => err = Some(e.context("calendar refresh failed; the last smart run's meetings are still showing")),
                    }
                }
                (r, err)
            }
        };
        self.state.lock().status.last_pass = match pass { Due::Morning => "morning", Due::CheckIn => "check-in", Due::Quick => "quick" }.into();
        merge_builtin(&mut raw, extra);
        let built = digest::build(&raw, &s, Local::now());
        let unchanged = self.state.lock().wire.as_deref().map(|old| same_content(old, &built.wire)).unwrap_or(false);
        if !unchanged {
            let _ = fs::create_dir_all(Settings::dir());
            let _ = fs::write(last_path(), &built.wire);
        }
        {
            let mut st = self.state.lock();
            st.status.unchanged = unchanged;
            // Nothing new: keep serving the same bytes, so the ETag holds, devices
            // get a 304, and no panel redraws for a timestamp.
            if !unchanged {
                st.etag = seal::etag(&built.wire);
                st.wire = Some(built.wire.clone());
                st.sealed.clear();
            }
            st.status.generated = now_l.timestamp();
            st.status.cards = built.cards;
            st.status.headline = built.headline.clone();
            st.status.dropped_private = built.dropped_private;
        }
        log::info!("digest: {} cards, {} bytes", built.cards, built.wire.len());
        if let Some(relay) = s.relay.as_ref().filter(|r| !r.url.is_empty() && !unchanged) {
            let ok = s.devices.iter().all(|d| match self.sealed_for(d) {
                Some((etag, blob)) => crate::relay::put(relay, d, &etag, &blob).is_ok(),
                None => false,
            });
            self.state.lock().status.relay_ok = Some(ok);
        }
        match err {
            Some(e) => Err(e.context("Claude run failed, so only the built-in screens were refreshed")),
            None => Ok(()),
        }
    }

    /// Screens the bridge makes itself, without Claude: weather, Claude Code,
    /// Cloudflare alerts and traffic, and the cached morning news (researched once a morning).
    pub fn builtin_cards(&self, s: &Settings, now: chrono::DateTime<Local>) -> Vec<serde_json::Value> {
        let mut out = vec![];
        if s.enabled("weather") && !s.weather_location.trim().is_empty() {
            match self.weather_card(s, now) {
                Ok(c) => out.push(c),
                Err(e) => log::warn!("weather: {e:#}"),
            }
        }
        if s.enabled("claude") {
            out.extend(crate::claude_usage::build(now));
        }
        if s.enabled("cloud") && !s.cloudflare_token.is_empty() {
            match crate::cloudflare::build(&s.cloudflare_token, &s.cloudflare_account, now) {
                Ok(c) => out.extend(c),
                Err(e) => log::warn!("cloudflare: {e:#}"),
            }
        }
        if s.enabled("traffic") && !s.cloudflare_token.is_empty() {
            match self.cf_traffic_card(s, now) {
                Ok(c) => out.extend(c),
                Err(e) => log::warn!("cloudflare traffic: {e:#}"),
            }
        }
        if s.enabled("news") {
            match crate::news::get(s, now) {
                Ok(c) => out.extend(c),
                Err(e) => log::warn!("news: {e:#}"),
            }
        }
        out
    }

    fn weather_card(&self, s: &Settings, now: chrono::DateTime<Local>) -> Result<serde_json::Value> {
        let q = s.weather_location.trim();
        let place = match &s.weather_place {
            Some(p) if p.query == q => p.clone(),
            _ => {
                let p = crate::weather::geocode(q)?;
                let mut w = self.settings.write();
                w.weather_place = Some(p.clone());
                let _ = w.save();
                p
            }
        };
        let key = format!("{}|{}", place.query, s.weather_fahrenheit);
        if let Some((at, k, c)) = self.state.lock().weather.clone() {
            if k == key && now.timestamp() - at < 30 * 60 {
                return Ok(c);
            }
        }
        let v = crate::weather::fetch(&place, s.weather_fahrenheit)?;
        let c = crate::weather::card(&v, &place, now)?;
        self.state.lock().weather = Some((now.timestamp(), key, c.clone()));
        Ok(c)
    }

    fn cf_traffic_card(&self, s: &Settings, now: chrono::DateTime<Local>) -> Result<Option<serde_json::Value>> {
        let key = format!("{}|{}", s.cloudflare_account, s.cloudflare_zone);
        if let Some((at, k, c)) = self.state.lock().cf_traffic.clone() {
            if k == key && now.timestamp() - at < 30 * 60 {
                return Ok(c);
            }
        }
        let c = crate::cloudflare::build_traffic(&s.cloudflare_token, &s.cloudflare_account, &s.cloudflare_zone, now)?;
        self.state.lock().cf_traffic = Some((now.timestamp(), key, c.clone()));
        Ok(c)
    }

    /// The digest sealed for one device, cached until the next run.
    pub fn sealed_for(&self, d: &Device) -> Option<(String, Vec<u8>)> {
        let mut st = self.state.lock();
        let wire = st.wire.clone()?;
        if let Some((e, b)) = st.sealed.get(&d.id) {
            if *e == st.etag {
                return Some((e.clone(), b.clone()));
            }
        }
        let blob = seal::seal(&wire, &d.key, &d.id).ok()?;
        let e = st.etag.clone();
        st.sealed.insert(d.id.clone(), (e.clone(), blob.clone()));
        Some((e, blob))
    }

    /// Starts the LAN server, mDNS advertisement and the schedule.
    pub fn start(&self) -> Result<()> {
        let port = self.settings.read().port;
        crate::server::spawn(self.clone(), port)?;
        self.state.lock().status.serving = true;
        let me = self.clone();
        std::thread::Builder::new().name("cink-schedule".into()).spawn(move || loop {
            let due = {
                let st = me.state.lock();
                st.status.next_run <= now() && !st.status.running
            };
            let s = me.settings.read().clone();
            let paused = s.pause_in_quiet && Self::in_quiet(&s);
            let ready = claude::find(&s.claude_path).is_some();
            if due && !paused && ready {
                let _ = me.run(false);
            } else if due {
                me.state.lock().status.next_run = now() + 300;
            }
            std::thread::sleep(Duration::from_secs(15));
        })?;
        Ok(())
    }

    /// Replaces the current digest (used by tests and `cink-bridge push`).
    pub fn set_wire(&self, wire: Vec<u8>) {
        let mut st = self.state.lock();
        st.etag = seal::etag(&wire);
        st.wire = Some(wire);
        st.sealed.clear();
    }

    pub fn touch_device(&self, id: &str) {
        self.state.lock().status.last_seen.insert(id.to_string(), now());
    }

    pub fn device(&self, id: &str) -> Option<Device> {
        self.settings.read().devices.iter().find(|d| d.id == id).cloned()
    }

    pub fn current_etag(&self) -> String {
        self.state.lock().etag.clone()
    }

    /// Adds or refreshes a device record (new key and token), returning it.
    pub fn pair(&self, id: &str, name: &str, firmware: &str) -> Result<Device> {
        if id.is_empty() {
            return Err(anyhow!("device did not report an id"));
        }
        let mut s = self.settings.read().clone();
        s.devices.retain(|d| d.id != id);
        let d = Device {
            id: id.into(),
            name: name.into(),
            key: seal::new_key(),
            token: seal::new_token(),
            firmware: firmware.into(),
            paired_at: now(),
        };
        s.devices.push(d.clone());
        self.update_settings(s)?;
        Ok(d)
    }
}

/// Adds the bridge's own cards to Claude's answer, replacing any card Claude
/// made for a bridge-only screen or with the same id. Traffic is shared:
/// Claude's Vercel card stays next to the bridge's Cloudflare one.
fn merge_builtin(raw: &mut serde_json::Value, extra: Vec<serde_json::Value>) {
    let ids: Vec<String> = extra.iter().filter_map(|c| c["id"].as_str().map(str::to_string)).collect();
    let mut cards: Vec<serde_json::Value> = raw["cards"]
        .as_array()
        .cloned()
        .unwrap_or_default()
        .into_iter()
        .filter(|c| !crate::kinds::BUILTIN.contains(&c["kind"].as_str().unwrap_or("")) && !ids.iter().any(|i| c["id"] == i.as_str()))
        .collect();
    cards.extend(extra);
    raw["cards"] = serde_json::Value::Array(cards);
}

/// True when two wire digests differ only in their generation time.
fn same_content(a: &[u8], b: &[u8]) -> bool {
    let strip = |w: &[u8]| {
        serde_json::from_slice::<serde_json::Value>(w).ok().map(|mut v| {
            if let Some(o) = v.as_object_mut() {
                o.remove("gen");
            }
            v
        })
    };
    matches!((strip(a), strip(b)), (Some(x), Some(y)) if x == y)
}

/// Folds the quick calendar pass into the last smart answer. Claude's words
/// stay; times follow the calendar; new meetings appear with plain text;
/// meetings that vanished from the calendar (cancelled, declined, moved past
/// the window) are dropped.
fn merge_events(raw: &mut serde_json::Value, quick: &serde_json::Value, now: chrono::DateTime<Local>) {
    use serde_json::{json, Value};
    let Some(events) = quick["events"].as_array() else { return };
    let mut cards: Vec<Value> = raw["cards"].as_array().cloned().unwrap_or_default();
    // Same meeting: same id, or (if the two runs spelled the id differently)
    // the same title.
    let same = |c: &Value, e: &Value| {
        c["kind"] == "event"
            && (c["id"] == e["id"]
                || c["title"].as_str().map(|t| t.trim().to_lowercase()) == e["title"].as_str().map(|t| t.trim().to_lowercase()))
    };
    let started = |c: &Value| {
        c["at"].as_str().and_then(|t| chrono::DateTime::parse_from_rfc3339(t).ok()).map(|t| t <= now).unwrap_or(true)
    };
    // A meeting already under way may have left the "from now" window; keep it.
    cards.retain(|c| c["kind"] != "event" || events.iter().any(|e| same(c, e)) || started(c));
    for e in events {
        let id = e["id"].as_str().unwrap_or("");
        if id.is_empty() {
            continue;
        }
        if let Some(c) = cards.iter_mut().find(|c| same(c, e)) {
            for k in ["at", "end"] {
                if e[k].is_string() {
                    c[k] = e[k].clone();
                }
            }
            if c["meta"].as_str().unwrap_or("").is_empty() && e["where"].is_string() {
                c["meta"] = e["where"].clone();
            }
            continue;
        }
        let mut c = json!({"id": id, "kind": "event", "title": e["title"], "at": e["at"], "priority": 50,
                           "src": e["src"].as_str().unwrap_or("gcal")});
        if e["end"].is_string() {
            c["end"] = e["end"].clone();
        }
        if let Some(w) = e["who"].as_str().filter(|w| !w.is_empty()) {
            c["subtitle"] = json!(format!("with {w}"));
        }
        if e["where"].is_string() {
            c["meta"] = e["where"].clone();
        }
        cards.push(c);
    }
    raw["cards"] = Value::Array(cards);
}

pub fn host_name() -> String {
    hostname::get().ok().and_then(|h| h.into_string().ok()).unwrap_or_else(|| "c-ink".into())
}

/// When the device should next show something new, for the UI's "next change".
pub fn next_change_local(wire: &[u8], disabled: u32) -> Option<String> {
    let t = crate::preview::next_change(wire, now(), disabled);
    Local.timestamp_opt(t, 0).single().map(|d| d.format("%-I:%M %p").to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bridge_traffic_sits_next_to_claudes() {
        let mut raw = serde_json::json!({"cards": [
            {"id": "vercel:traffic", "kind": "traffic"},
            {"id": "wx", "kind": "weather"},
            {"id": "cf:traffic", "kind": "traffic"}]});
        merge_builtin(&mut raw, vec![serde_json::json!({"id": "cf:traffic", "kind": "traffic", "src": "cloudflare"}),
                                     serde_json::json!({"id": "weather:now", "kind": "weather"})]);
        let ids: Vec<&str> = raw["cards"].as_array().unwrap().iter().map(|c| c["id"].as_str().unwrap()).collect();
        assert_eq!(ids, vec!["vercel:traffic", "cf:traffic", "weather:now"]);
    }

    fn get(url: &str, token: &str, etag: Option<&str>) -> (u16, Vec<u8>, String) {
        let mut r = ureq::get(url).set("Authorization", &format!("Bearer {token}"));
        if let Some(e) = etag {
            r = r.set("If-None-Match", e);
        }
        match r.call() {
            Ok(resp) => {
                let st = resp.status();
                let et = resp.header("ETag").unwrap_or("").to_string();
                let mut body = vec![];
                std::io::Read::read_to_end(&mut resp.into_reader(), &mut body).unwrap();
                (st, body, et)
            }
            Err(ureq::Error::Status(code, _)) => (code, vec![], String::new()),
            Err(e) => panic!("{e}"),
        }
    }

    #[test]
    fn passes_follow_the_day() {
        let mut s = Settings::default();
        let at = |h| Local::now().date_naive().and_hms_opt(h, 0, 0).unwrap().and_local_timezone(Local).earliest().unwrap();
        let today = at(9).format("%Y-%m-%d").to_string();
        assert_eq!(due(&s, 0, "", at(9), false), Due::Morning);
        assert_eq!(due(&s, at(8).timestamp(), "2001-01-01", at(9), false), Due::Morning);
        assert_eq!(due(&s, at(9).timestamp(), &today, at(9) + chrono::Duration::minutes(15), false), Due::Quick);
        assert_eq!(due(&s, at(9).timestamp(), &today, at(10), false), Due::CheckIn);
        assert_eq!(due(&s, at(9).timestamp(), &today, at(9) + chrono::Duration::minutes(15), true), Due::CheckIn);
        s.smart_every_min = 0;
        assert_eq!(due(&s, at(9).timestamp(), &today, at(15), false), Due::Quick);
    }

    #[test]
    fn quick_pass_moves_adds_and_drops_meetings() {
        let now = Local::now();
        let t = |m: i64| (now + chrono::Duration::minutes(m)).to_rfc3339();
        let mut raw = serde_json::json!({"cards": [
            {"id": "cal:a", "kind": "event", "title": "Design review", "body": "Priya wants a yes or no", "at": t(30), "priority": 70},
            {"id": "cal:gone", "kind": "event", "title": "Cancelled 1:1", "at": t(90), "priority": 50},
            {"id": "cal:now", "kind": "event", "title": "Standup", "at": t(-5), "priority": 50},
            {"id": "slack:triage", "kind": "chat", "title": "Slack", "priority": 60}]});
        let q = serde_json::json!({"events": [
            {"id": "cal:a", "title": "Design review", "at": t(45), "end": t(75), "where": "Google Meet"},
            {"id": "cal:new", "title": "Coffee with Dee", "at": t(120), "who": "Dee", "src": "gcal"}]});
        merge_events(&mut raw, &q, now);
        let cards = raw["cards"].as_array().unwrap();
        let ids: Vec<&str> = cards.iter().map(|c| c["id"].as_str().unwrap()).collect();
        assert_eq!(ids, vec!["cal:a", "cal:now", "slack:triage", "cal:new"]);
        assert_eq!(cards[0]["at"], t(45));
        assert_eq!(cards[0]["body"], "Priya wants a yes or no");
        assert_eq!(cards[3]["subtitle"], "with Dee");
    }

    #[test]
    fn identical_digests_keep_their_etag() {
        assert!(same_content(br#"{"v":1,"gen":5,"cards":[1]}"#, br#"{"v":1,"gen":9,"cards":[1]}"#));
        assert!(!same_content(br#"{"v":1,"gen":5,"cards":[1]}"#, br#"{"v":1,"gen":9,"cards":[2]}"#));
    }

    #[test]
    fn lan_endpoint_serves_sealed_digest() {
        let mut s = Settings::default();
        let dev = Device { id: "AB12-CD34".into(), name: "t".into(), key: seal::new_key(), token: "tok".into(),
                           firmware: "0".into(), paired_at: 0 };
        s.devices.push(dev.clone());
        let b = Bridge { settings: Arc::new(RwLock::new(s)), state: Default::default(), run_lock: Default::default() };
        let port = crate::server::spawn(b.clone(), 0).unwrap();
        let url = format!("http://127.0.0.1:{port}/v1/digest/{}", dev.id);

        assert_eq!(get(&url, "tok", None).0, 503); // nothing yet
        b.set_wire(br#"{"v":1,"gen":5,"cards":[]}"#.to_vec());
        assert_eq!(get(&url, "wrong", None).0, 401);
        let (st, blob, etag) = get(&url, "tok", None);
        assert_eq!(st, 200);
        assert_eq!(seal::open(&blob, &dev.key, &dev.id).unwrap(), br#"{"v":1,"gen":5,"cards":[]}"#);
        assert_eq!(get(&url, "tok", Some(&etag)).0, 304);
        assert!(b.status().last_seen.contains_key(&dev.id));
        b.set_wire(br#"{"v":1,"gen":6,"cards":[]}"#.to_vec());
        assert_eq!(get(&url, "tok", Some(&etag)).0, 200);
    }
}
