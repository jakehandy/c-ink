//! Turns Claude's structured answer into the compact wire digest the device
//! parses (docs/protocol.md): trimmed to what the screen can show, times as
//! unix seconds, private terms filtered a second time, cards under 20 KB.
//! One dithered photo (the Top story's) may ride along as "img", up to
//! IMG_MAX_BYTES more; the device caches up to 32 KB.

use chrono::{DateTime, Local};
use serde_json::{json, Map, Value};

use crate::settings::Settings;

pub const MAX_BYTES: usize = 20 * 1024;
pub const IMG_MAX_BYTES: usize = 9 * 1024;
const MAX_CARDS: usize = 24;

fn text(v: &Value, key: &str, max_chars: usize) -> String {
    let s = v.get(key).and_then(Value::as_str).unwrap_or("");
    clean(s, max_chars)
}

/// Collapses whitespace, drops control characters and emoji, and cuts at a
/// word boundary when over the limit.
pub fn clean(s: &str, max_chars: usize) -> String {
    let mut out = String::with_capacity(s.len());
    let mut space = false;
    for ch in s.chars() {
        let c = if ch.is_whitespace() { ' ' } else { ch };
        if c.is_control() || (c as u32) >= 0x1F000 || (0x2600..0x27C0).contains(&(c as u32)) {
            continue;
        }
        if c == ' ' {
            if space || out.is_empty() {
                continue;
            }
            space = true;
        } else {
            space = false;
        }
        out.push(c);
    }
    let out = out.trim_end().to_string();
    if out.chars().count() <= max_chars {
        return out;
    }
    let cut: String = out.chars().take(max_chars - 1).collect();
    let cut = match cut.rfind(' ') {
        Some(i) if i > max_chars / 2 => cut[..i].to_string(),
        _ => cut,
    };
    format!("{}\u{2026}", cut.trim_end_matches([' ', ',', '.', ';', ':']))
}

fn time(v: &Value, key: &str) -> Option<i64> {
    let s = v.get(key)?.as_str()?;
    DateTime::parse_from_rfc3339(s.trim()).ok().map(|d| d.timestamp())
}

fn mentions_private(v: &Value, terms: &[String]) -> bool {
    if terms.is_empty() {
        return false;
    }
    let hay = v.to_string().to_lowercase();
    terms.iter().map(|t| t.trim().to_lowercase()).filter(|t| t.len() >= 2).any(|t| hay.contains(&t))
}

/// Settings that ride along to the device with every digest.
pub fn device_cfg(s: &Settings) -> Value {
    let (qa, qb) = s.quiet.map(|(a, b)| (a as i64, b as i64)).unwrap_or((-1, -1));
    json!({
        "rf": s.refresh_min,
        "dis": s.disabled_mask(),
        "q": [qa, qb],
        "mono": s.mono,
        "rot": if s.flip { 1 } else { 0 },
        "tz": crate::tz::posix(&crate::tz::iana()),
    })
}

pub struct Built {
    pub wire: Vec<u8>,
    pub cards: usize,
    pub dropped_private: usize,
    pub headline: String,
}

pub fn build(raw: &Value, s: &Settings, now: DateTime<Local>) -> Built {
    let gen = now.timestamp();
    let mut dropped_private = 0;
    let mut cards: Vec<(i64, Value)> = vec![];
    let mut images: Vec<(String, Value)> = vec![];
    for c in raw.get("cards").and_then(Value::as_array).cloned().unwrap_or_default() {
        let kind = c.get("kind").and_then(Value::as_str).unwrap_or("other");
        let kind = if crate::kinds::index_of(kind).is_some() { kind } else { "other" };
        if !s.enabled(kind) {
            continue;
        }
        let mut head = c.clone();
        if let Some(m) = head.as_object_mut() {
            m.remove("rows"); // rows are filtered one by one below
            m.remove("image"); // pixels, not words (base64 would trip the term check)
        }
        if mentions_private(&head, &s.private_terms) {
            dropped_private += 1;
            continue;
        }
        let title = text(&c, "title", 90);
        if title.is_empty() {
            continue;
        }
        let mut o = Map::new();
        o.insert("id".into(), json!(text(&c, "id", 38)));
        o.insert("k".into(), json!(kind));
        o.insert("t".into(), json!(title));
        for (from, to, max) in [("short", "sh", 22), ("subtitle", "s", 90), ("body", "b", 220), ("meta", "m", 64),
                                ("big", "big", 12), ("delta", "dl", 36)] {
            let v = text(&c, from, max);
            if !v.is_empty() {
                o.insert(to.into(), json!(v));
            }
        }
        let src = text(&c, "src", 11).to_lowercase();
        if !src.is_empty() && src.chars().all(|ch| ch.is_ascii_alphanumeric()) {
            o.insert("src".into(), json!(src));
        }
        let wx = text(&c, "wx", 8);
        if ["clear", "partly", "cloudy", "rain", "storm", "snow", "fog"].contains(&wx.as_str()) {
            o.insert("wx".into(), json!(wx));
        }
        if let Some(sec) = c.get("sections").and_then(Value::as_array) {
            let names: Vec<String> = sec.iter().take(3).map(|v| clean(v.as_str().unwrap_or(""), 15).to_uppercase()).collect();
            if names.iter().any(|n| !n.is_empty()) {
                o.insert("sec".into(), json!(names));
            }
        }
        let p = c.get("priority").and_then(Value::as_i64).unwrap_or(50).clamp(0, 100);
        o.insert("p".into(), json!(p));
        if c.get("urgent").and_then(Value::as_bool) == Some(true) {
            o.insert("u".into(), json!(true));
        }
        let at = time(&c, "at");
        let mut end = time(&c, "end");
        let start = time(&c, "start");
        if let Some(e) = end {
            if e <= gen {
                continue; // already over
            }
        }
        if at.is_some() && end.is_none() && kind == "event" {
            end = at.map(|a| a + 30 * 60);
        }
        for (k, v) in [("at", at), ("st", start), ("en", end)] {
            if let Some(v) = v {
                o.insert(k.into(), json!(v));
            }
        }
        if let Some(sp) = c.get("spark").and_then(Value::as_array) {
            let nums: Vec<f64> = sp.iter().filter_map(Value::as_f64).take(24).collect();
            if nums.len() >= 2 {
                let (lo, hi) = nums.iter().fold((f64::MAX, f64::MIN), |(a, b), &x| (a.min(x), b.max(x)));
                let span = if hi > lo { hi - lo } else { 1.0 };
                let scaled: Vec<i64> = nums.iter().map(|x| ((x - lo) / span * 100.0).round() as i64).collect();
                o.insert("sp".into(), json!(scaled));
            }
        }
        if let Some(rows) = c.get("rows").and_then(Value::as_array) {
            let mut out = vec![];
            for r in rows.iter().take(6) {
                if mentions_private(r, &s.private_terms) {
                    dropped_private += 1;
                    continue;
                }
                let label = text(r, "label", 26);
                if label.is_empty() {
                    continue;
                }
                let mut ro = Map::new();
                ro.insert("l".into(), json!(label));
                let d = text(r, "detail", 90);
                if !d.is_empty() {
                    ro.insert("d".into(), json!(d));
                }
                let g = text(r, "badge", 4);
                if !g.is_empty() {
                    ro.insert("g".into(), json!(g));
                }
                ro.insert("tr".into(), json!(r.get("tier").and_then(Value::as_i64).unwrap_or(1).clamp(0, 2)));
                if r.get("urgent").and_then(Value::as_bool) == Some(true) {
                    ro.insert("u".into(), json!(true));
                }
                out.push(Value::Object(ro));
            }
            // Keep tiers together, most pressing first.
            out.sort_by_key(|r| r["tr"].as_i64().unwrap_or(1));
            if !out.is_empty() {
                o.insert("r".into(), json!(out));
            } else if matches!(kind, "chat" | "review" | "mail" | "news" | "cloud" | "parcel" | "db" | "support" | "teams") {
                continue; // a list card with nothing left in it
            }
        }
        if let Some(img) = c.get("image").filter(|i| i["k"].is_string()) {
            images.push((o["id"].as_str().unwrap_or("").to_string(), img.clone()));
        }
        cards.push((p, Value::Object(o)));
    }
    cards.sort_by_key(|c| std::cmp::Reverse(c.0));
    cards.truncate(MAX_CARDS);

    let headline = text(raw, "headline", 110);
    loop {
        let doc = json!({
            "v": 1,
            "gen": gen,
            "hz": gen + 12 * 3600,
            "hl": headline,
            "cards": cards.iter().map(|c| &c.1).collect::<Vec<_>>(),
            "cfg": device_cfg(s),
        });
        let wire = serde_json::to_vec(&doc).unwrap();
        if wire.len() <= MAX_BYTES || cards.is_empty() {
            // The photo goes only with a card that made it in.
            let img = images.iter().find(|(id, _)| !id.is_empty() && cards.iter().any(|c| c.1["id"] == id.as_str()));
            if let Some((id, i)) = img {
                let mut im = json!({"id": id, "w": i["w"], "h": i["h"], "k": i["k"]});
                if i["a"].is_string() {
                    im["a"] = i["a"].clone();
                }
                let mut with = doc.clone();
                with["img"] = im;
                let w2 = serde_json::to_vec(&with).unwrap();
                if w2.len() <= wire.len() + IMG_MAX_BYTES {
                    return Built { wire: w2, cards: cards.len(), dropped_private, headline };
                }
            }
            return Built { wire, cards: cards.len(), dropped_private, headline };
        }
        cards.pop(); // drop the lowest priority card until it fits
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clean_cuts_on_words() {
        assert_eq!(clean("  hello \n  world  ", 50), "hello world");
        let c = clean("the quick brown fox jumps over the lazy dog", 20);
        assert!(c.chars().count() <= 20 && c.ends_with('\u{2026}'), "{c}");
        assert_eq!(clean("ship it \u{1F680}", 20), "ship it");
    }

    #[test]
    fn build_filters_and_converts() {
        let now = Local::now();
        let mut s = Settings::default();
        s.private_terms = vec!["secret project".into()];
        s.screens.insert("money".into(), false);
        let at = (now + chrono::Duration::minutes(30)).to_rfc3339();
        let raw = json!({"headline": "Busy", "cards": [
            {"id": "a", "kind": "event", "title": "Standup", "priority": 50, "at": at},
            {"id": "b", "kind": "money", "title": "Revenue", "priority": 90},
            {"id": "c", "kind": "chat", "title": "Slack", "priority": 60, "rows": [
                {"label": "Sam", "detail": "about the Secret Project", "tier": 0},
                {"label": "Ana", "detail": "lunch?", "tier": 2}]},
            {"id": "d", "kind": "mail", "title": "Mail", "priority": 40, "rows": [{"label": "x", "detail": "secret project"}]},
            {"id": "e", "kind": "weird", "title": "Other thing", "priority": 10},
            {"id": "f", "kind": "event", "title": "Over", "priority": 99, "end": "2001-01-01T00:00:00Z"}
        ]});
        let b = build(&raw, &s, now);
        let v: Value = serde_json::from_slice(&b.wire).unwrap();
        let ids: Vec<&str> = v["cards"].as_array().unwrap().iter().map(|c| c["id"].as_str().unwrap()).collect();
        assert_eq!(ids, vec!["c", "a", "e"]);
        assert_eq!(v["cards"][0]["r"].as_array().unwrap().len(), 1);
        assert_eq!(b.dropped_private, 2);
        assert!(v["cards"][1]["at"].as_i64().unwrap() > now.timestamp());
        assert_eq!(v["cards"][2]["k"], "other");
    }

    #[test]
    fn passes_source_weather_and_sections() {
        let s = Settings::default();
        let raw = json!({"headline": "", "cards": [
            {"id": "m", "kind": "mail", "title": "Inbox", "priority": 50, "src": "gmail",
             "sections": ["Work", "personal", "a very long section name"], "rows": [{"label": "Ana", "tier": 1}]},
            {"id": "w", "kind": "weather", "title": "Rain at 3", "priority": 30, "wx": "rain", "src": "bad id!"},
            {"id": "x", "kind": "weather", "title": "?", "priority": 20, "wx": "tornado"}]});
        let v: Value = serde_json::from_slice(&build(&raw, &s, Local::now()).wire).unwrap();
        let c = &v["cards"];
        assert_eq!(c[0]["src"], "gmail");
        assert_eq!(c[0]["sec"][1], "PERSONAL");
        assert!(c[0]["sec"][2].as_str().unwrap().chars().count() <= 15);
        assert_eq!(c[1]["wx"], "rain");
        assert!(c[1].get("src").is_none());
        assert!(c[2].get("wx").is_none());
    }

    #[test]
    fn size_cap() {
        let s = Settings::default();
        let cards: Vec<Value> = (0..40).map(|i| json!({"id": format!("x{i}"), "kind": "other",
            "title": "t".repeat(80), "body": "b ".repeat(110), "priority": i})).collect();
        let b = build(&json!({"headline": "h", "cards": cards}), &s, Local::now());
        assert!(b.wire.len() <= MAX_BYTES);
        assert!(b.cards > 0 && b.cards <= 24);
    }

    #[test]
    fn story_photo_rides_along_with_its_card() {
        let s = Settings::default();
        let img = crate::photo::wire(&image::RgbImage::from_pixel(216, 120, image::Rgb([90, 90, 90])), false);
        let raw = json!({"cards": [
            {"id": "story:1", "kind": "story", "title": "Big news", "priority": 30, "image": img},
            {"id": "x", "kind": "chat", "title": "Slack", "rows": [{"label": "a"}]}]});
        let b = build(&raw, &s, Local::now());
        let v: Value = serde_json::from_slice(&b.wire).unwrap();
        assert_eq!(v["img"]["id"], "story:1");
        assert_eq!(v["img"]["w"], 216);
        assert!(v["cards"][0].get("image").is_none());
        assert!(b.wire.len() <= MAX_BYTES + IMG_MAX_BYTES);
        let mut off = s.clone();
        off.screens.insert("story".into(), false);
        let v: Value = serde_json::from_slice(&build(&raw, &off, Local::now()).wire).unwrap();
        assert!(v.get("img").is_none());
    }
}
