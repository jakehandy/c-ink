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
    pub serving: bool,
    pub port: u16,
    pub host: String,
    pub last_seen: HashMap<String, i64>,
    pub relay_ok: Option<bool>,
}

#[derive(Default)]
struct State {
    status: Status,
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

    /// One full digest run. Safe to call from any thread; concurrent calls wait.
    pub fn run_now(&self) -> Result<Status> {
        let _g = self.run_lock.lock();
        self.state.lock().status.running = true;
        let started = std::time::Instant::now();
        let result = self.run_inner();
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

    fn run_inner(&self) -> Result<()> {
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
        let has_github = s.tools.iter().any(|t| t.to_lowercase().contains("github"));
        let gh = if s.enabled("review") && !has_github { claude::gh_context() } else { None };
        let now_l = Local::now();
        let p = prompt::build(&s, now_l, &s.tools, gh.as_deref());
        let raw = claude::run_digest(&s.claude_path, &s.model, &p, prompt::SCHEMA, &s.tools)?;
        let built = digest::build(&raw, &s, Local::now());
        let _ = fs::create_dir_all(Settings::dir());
        let _ = fs::write(last_path(), &built.wire);
        {
            let mut st = self.state.lock();
            st.etag = seal::etag(&built.wire);
            st.wire = Some(built.wire.clone());
            st.sealed.clear();
            st.status.generated = now_l.timestamp();
            st.status.cards = built.cards;
            st.status.headline = built.headline.clone();
            st.status.dropped_private = built.dropped_private;
        }
        log::info!("digest: {} cards, {} bytes", built.cards, built.wire.len());
        if let Some(relay) = s.relay.as_ref().filter(|r| !r.url.is_empty()) {
            let ok = s.devices.iter().all(|d| match self.sealed_for(d) {
                Some((etag, blob)) => crate::relay::put(relay, d, &etag, &blob).is_ok(),
                None => false,
            });
            self.state.lock().status.relay_ok = Some(ok);
        }
        Ok(())
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
                let _ = me.run_now();
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
