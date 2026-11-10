//! Cloudflare screens, built by the bridge with a read-only API token.
//! Claude is not involved.
//!
//! Alerts come from Cloudflare's notification history; Traffic comes from
//! zone analytics (GraphQL, `httpRequestsAdaptiveGroups`, kept 31 days on
//! every plan). Token permissions: Account > Notifications > Read, Account >
//! Account Settings > Read (to find the account), and for Traffic, Zone >
//! Zone > Read and Zone > Analytics > Read.

use std::collections::BTreeMap;

use anyhow::{anyhow, bail, Context, Result};
use chrono::{DateTime, Local, TimeZone};
use serde_json::{json, Value};

const API: &str = "https://api.cloudflare.com/client/v4";

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new().timeout(std::time::Duration::from_secs(15)).user_agent("C-Ink").build()
}

fn get(token: &str, path: &str, query: &[(&str, &str)]) -> Result<Value> {
    let mut r = agent().get(&format!("{API}{path}")).set("Authorization", &format!("Bearer {}", token.trim()));
    for (k, v) in query {
        r = r.query(k, v);
    }
    let v: Value = match r.call() {
        Ok(resp) => resp.into_json()?,
        Err(ureq::Error::Status(_, resp)) => resp.into_json().unwrap_or(Value::Null),
        Err(e) => return Err(e).context("reaching Cloudflare"),
    };
    if v["success"].as_bool() != Some(true) {
        let msg = v["errors"][0]["message"].as_str().unwrap_or("Cloudflare refused the request");
        bail!("{msg}");
    }
    Ok(v)
}

fn graphql(token: &str, query: &str, vars: Value) -> Result<Value> {
    let r = agent()
        .post(&format!("{API}/graphql"))
        .set("Authorization", &format!("Bearer {}", token.trim()))
        .send_json(json!({"query": query, "variables": vars}));
    let v: Value = match r {
        Ok(resp) => resp.into_json()?,
        Err(ureq::Error::Status(_, resp)) => resp.into_json().unwrap_or(Value::Null),
        Err(e) => return Err(e).context("reaching Cloudflare"),
    };
    if let Some(e) = v["errors"].as_array().and_then(|e| e.first()) {
        bail!("{}", e["message"].as_str().unwrap_or("Cloudflare analytics refused the query"));
    }
    if v["data"].is_null() {
        bail!("Cloudflare analytics returned nothing");
    }
    Ok(v["data"].clone())
}

/// The account's zones (sites), as (id, name), up to 50.
pub fn zones(token: &str, account: &str) -> Result<Vec<(String, String)>> {
    let v = get(token, "/zones", &[("account.id", account), ("per_page", "50"), ("status", "active")])?;
    Ok(v["result"]
        .as_array()
        .map(|a| {
            a.iter()
                .filter_map(|z| Some((z["id"].as_str()?.to_string(), z["name"].as_str()?.to_string())))
                .collect()
        })
        .unwrap_or_default())
}

/// What the Traffic screen counts: pages (HTML responses) served to real
/// visitors, so assets, API calls and Cloudflare's own probes stay out.
const PAGES: &str = r#"requestSource: "eyeball", edgeResponseContentTypeName: "html""#;

fn utc(t: DateTime<Local>) -> String {
    t.with_timezone(&chrono::Utc).format("%Y-%m-%dT%H:%M:%SZ").to_string()
}

/// Today's page views (since local midnight) for each zone, to find the busiest.
fn busiest(token: &str, zones: &[(String, String)], now: DateTime<Local>) -> Result<Option<(String, String)>> {
    let (a, b) = (utc(day_start(now)), utc(now));
    let ids: Vec<&str> = zones.iter().take(10).map(|z| z.0.as_str()).collect();
    let q = format!(
        "query($z: [string!], $a: Time!, $b: Time!) {{ viewer {{ zones(filter: {{zoneTag_in: $z}}) {{ zoneTag \
         t: httpRequestsAdaptiveGroups(limit: 1, filter: {{datetime_geq: $a, datetime_lt: $b, {PAGES}}}) {{ count }} }} }} }}"
    );
    let d = graphql(token, &q, json!({"z": ids, "a": a, "b": b}))?;
    let best = d["viewer"]["zones"]
        .as_array()
        .into_iter()
        .flatten()
        .map(|z| (z["zoneTag"].as_str().unwrap_or("").to_string(), z["t"][0]["count"].as_i64().unwrap_or(0)))
        .max_by_key(|z| z.1);
    Ok(best.and_then(|(id, _)| zones.iter().find(|z| z.0 == id).cloned()))
}

fn day_start(now: DateTime<Local>) -> DateTime<Local> {
    now.date_naive().and_hms_opt(0, 0, 0).and_then(|d| Local.from_local_datetime(&d).earliest()).unwrap_or(now)
}

/// Raw numbers for the Traffic card: hourly page views today, the same
/// window a week ago, the top page and the top outside referrer.
pub struct Traffic {
    pub zone: String,
    pub hourly: Vec<i64>,
    pub today: i64,
    pub last_week: i64,
    pub top_path: String,
    pub top_referrer: String,
}

pub fn traffic(token: &str, zone: &(String, String), now: DateTime<Local>) -> Result<Traffic> {
    let start = day_start(now);
    let week = chrono::Duration::days(7);
    let q = format!(
        "query($z: string, $a: Time!, $b: Time!, $wa: Time!, $wb: Time!) {{ viewer {{ zones(filter: {{zoneTag: $z}}) {{ \
         hours: httpRequestsAdaptiveGroups(limit: 30, orderBy: [datetimeHour_ASC], filter: {{datetime_geq: $a, datetime_lt: $b, {PAGES}}}) {{ count dimensions {{ datetimeHour }} }} \
         week: httpRequestsAdaptiveGroups(limit: 1, filter: {{datetime_geq: $wa, datetime_lt: $wb, {PAGES}}}) {{ count }} \
         paths: httpRequestsAdaptiveGroups(limit: 1, orderBy: [count_DESC], filter: {{datetime_geq: $a, datetime_lt: $b, {PAGES}}}) {{ count dimensions {{ clientRequestPath }} }} \
         refs: httpRequestsAdaptiveGroups(limit: 6, orderBy: [count_DESC], filter: {{datetime_geq: $a, datetime_lt: $b, clientRefererHost_neq: \"\", {PAGES}}}) {{ count dimensions {{ clientRefererHost }} }} \
         }} }} }}"
    );
    let d = graphql(token, &q, json!({"z": zone.0, "a": utc(start), "b": utc(now), "wa": utc(start - week), "wb": utc(now - week)}))?;
    let z = &d["viewer"]["zones"][0];
    let list = |k: &str| z[k].as_array().cloned().unwrap_or_default();
    let hourly: Vec<i64> = list("hours").iter().map(|h| h["count"].as_i64().unwrap_or(0)).collect();
    let site = zone.1.to_lowercase();
    let top_referrer = list("refs")
        .iter()
        .filter_map(|r| r["dimensions"]["clientRefererHost"].as_str())
        .map(|h| h.to_lowercase())
        .find(|h| !(h == &site || h.ends_with(&format!(".{site}"))))
        .unwrap_or_default();
    Ok(Traffic {
        zone: zone.1.clone(),
        today: hourly.iter().sum(),
        hourly,
        last_week: z["week"][0]["count"].as_i64().unwrap_or(0),
        top_path: list("paths").first().and_then(|p| p["dimensions"]["clientRequestPath"].as_str()).unwrap_or("").to_string(),
        top_referrer,
    })
}

fn thousands(n: i64) -> String {
    let s = n.abs().to_string();
    let mut out = String::new();
    for (i, ch) in s.chars().enumerate() {
        if i > 0 && (s.len() - i).is_multiple_of(3) {
            out.push(',');
        }
        out.push(ch);
    }
    if n < 0 { format!("-{out}") } else { out }
}

fn pretty_referrer(h: &str) -> String {
    let h = h.trim_start_matches("www.").trim_start_matches("m.");
    match h {
        "news.ycombinator.com" => "Hacker News".into(),
        "t.co" | "x.com" | "twitter.com" => "X".into(),
        "l.facebook.com" | "lm.facebook.com" | "facebook.com" => "Facebook".into(),
        "out.reddit.com" | "old.reddit.com" | "reddit.com" => "Reddit".into(),
        "linkedin.com" | "lnkd.in" => "LinkedIn".into(),
        "bsky.app" => "Bluesky".into(),
        h if h.starts_with("google.") => "Google".into(),
        h => h.to_string(),
    }
}

/// The Traffic card for one zone, or None when nothing was served today.
pub fn traffic_card(t: &Traffic, now: DateTime<Local>) -> Option<Value> {
    if t.today <= 0 {
        return None;
    }
    let weekday = now.format("%A").to_string();
    let pct = if t.last_week >= 20 { Some(((t.today - t.last_week) as f64 / t.last_week as f64 * 100.0).round() as i64) } else { None };
    let (title, short, priority) = match pct {
        Some(p) if p >= 30 => (format!("{} is busier than usual", t.zone), format!("Traffic up {p}%"), 42),
        Some(p) if p <= -30 => (format!("{} is quieter than usual", t.zone), format!("Traffic down {}%", -p), 40),
        Some(p) if p.abs() >= 15 => (format!("{} traffic is {}", t.zone, if p > 0 { "up" } else { "down" }), format!("Traffic {} {}%", if p > 0 { "up" } else { "down" }, p.abs()), 28),
        _ => (format!("A normal day on {}", t.zone), "Traffic steady".to_string(), 15),
    };
    let delta = match pct {
        Some(p) => format!("{}{p}% vs last {weekday}", if p > 0 { "+" } else { "" }),
        None => "page views today".to_string(),
    };
    let mut sub = vec![];
    if !t.top_path.is_empty() {
        sub.push(t.top_path.clone());
    }
    if !t.top_referrer.is_empty() {
        sub.push(format!("most from {}", pretty_referrer(&t.top_referrer)));
    }
    let mut c = json!({
        "id": "cf:traffic",
        "kind": "traffic",
        "src": "cloudflare",
        "title": title,
        "short": short,
        "big": thousands(t.today),
        "delta": delta,
        "priority": priority,
        "start": now.to_rfc3339(),
        "end": (now + chrono::Duration::hours(3)).to_rfc3339(),
    });
    if !sub.is_empty() {
        c["subtitle"] = json!(sub.join("  ·  "));
    }
    if t.hourly.len() >= 2 {
        c["spark"] = json!(t.hourly);
    }
    Some(c)
}

/// Picks the zone (the one named in settings, else today's busiest) and builds
/// its Traffic card.
pub fn build_traffic(token: &str, account: &str, zone: &str, now: DateTime<Local>) -> Result<Option<Value>> {
    if token.trim().is_empty() || account.trim().is_empty() {
        return Ok(None);
    }
    let all = zones(token, account)?;
    let picked = match zone.trim() {
        "" if all.len() == 1 => all.first().cloned(),
        "" => busiest(token, &all, now)?,
        z => all.iter().find(|(id, name)| id == z || name.eq_ignore_ascii_case(z)).cloned(),
    };
    match picked {
        Some(z) => Ok(traffic_card(&traffic(token, &z, now)?, now)),
        None => Ok(None),
    }
}

/// Checks the token and finds the account. Returns (account id, account name).
pub fn verify(token: &str, account: &str) -> Result<(String, String)> {
    if token.trim().is_empty() {
        bail!("paste a Cloudflare API token first");
    }
    if !account.trim().is_empty() {
        // Account-owned tokens verify against the account; user tokens against /user.
        if get(token, &format!("/accounts/{}/tokens/verify", account.trim()), &[]).is_err() {
            get(token, "/user/tokens/verify", &[])?;
        }
        let name = get(token, &format!("/accounts/{}", account.trim()), &[])
            .ok()
            .and_then(|v| v["result"]["name"].as_str().map(str::to_string))
            .unwrap_or_default();
        return Ok((account.trim().to_string(), name));
    }
    get(token, "/user/tokens/verify", &[])?;
    let v = get(token, "/accounts", &[("per_page", "5")])?;
    let a = &v["result"][0];
    let id = a["id"].as_str().ok_or_else(|| anyhow!("this token cannot see any account; add Account Settings: Read or paste the account id"))?;
    Ok((id.to_string(), a["name"].as_str().unwrap_or("").to_string()))
}

/// Alert history for the last 24 hours, newest first.
pub fn history(token: &str, account: &str, now: DateTime<Local>) -> Result<Vec<Value>> {
    let since = (now - chrono::Duration::hours(24)).to_rfc3339();
    let before = now.to_rfc3339();
    let v = get(token, &format!("/accounts/{account}/alerting/v3/history"),
                &[("since", &since), ("before", &before), ("per_page", "50")])?;
    Ok(v["result"].as_array().cloned().unwrap_or_default())
}

/// How pressing an alert type is: (tier, short label, badge).
fn classify(alert_type: &str) -> (i64, &'static str, &'static str) {
    let t = alert_type.to_lowercase();
    match () {
        _ if t.contains("dos_attack") || t.contains("ddos") => (0, "DDoS attack", "!"),
        _ if t.contains("origin") || t.contains("health_check") || t.contains("healthcheck") || t.contains("load_balancing") => (0, "Origin down", "!"),
        _ if t.contains("tunnel") => (0, "Tunnel down", "!"),
        _ if t.contains("incident") => (1, "Cloudflare incident", ""),
        _ if t.contains("workers") || t.contains("script") => (1, "Workers errors", ""),
        _ if t.contains("ssl") || t.contains("certificate") || t.contains("universal") => (1, "Certificate", "SSL"),
        _ if t.contains("firewall") || t.contains("waf") || t.contains("security") => (1, "Security events", ""),
        _ if t.contains("billing") || t.contains("usage") => (2, "Usage", ""),
        _ => (2, "Alert", ""),
    }
}

fn clock(t: i64) -> String {
    Local.timestamp_opt(t, 0).single().map(|d| d.format("%-I:%M %p").to_string()).unwrap_or_default()
}

/// The Cloudflare card, or None when nothing fired in the last day.
pub fn card(alerts: &[Value], now: DateTime<Local>) -> Option<Value> {
    // Group by alert type and the policy that sent it.
    let mut groups: BTreeMap<(i64, String, String), (usize, i64, String)> = BTreeMap::new();
    for a in alerts {
        let ty = a["alert_type"].as_str().unwrap_or("");
        let (tier, label, _) = classify(ty);
        let sent = a["sent"].as_str().and_then(|s| DateTime::parse_from_rfc3339(s).ok()).map(|d| d.timestamp()).unwrap_or(0);
        let what = a["name"].as_str().or(a["description"].as_str()).unwrap_or("").to_string();
        let e = groups.entry((tier, label.to_string(), ty.to_string())).or_insert((0, 0, String::new()));
        e.0 += 1;
        if sent >= e.1 {
            e.1 = sent;
            e.2 = what;
        }
    }
    if groups.is_empty() {
        return None;
    }
    let mut rows: Vec<Value> = groups
        .iter()
        .map(|((tier, label, ty), (n, last, what))| {
            let badge = match classify(ty).2 { "" if *n > 1 => n.to_string(), b => b.to_string() };
            let detail = if what.is_empty() { format!("Last at {}", clock(*last)) } else { format!("{what}, {}", clock(*last)) };
            json!({"label": label, "detail": detail, "tier": tier, "badge": badge, "urgent": *tier == 0 && now.timestamp() - last < 3600})
        })
        .collect();
    rows.sort_by_key(|r| r["tier"].as_i64().unwrap_or(2));
    rows.truncate(6);
    let total: usize = groups.values().map(|g| g.0).sum();
    let worst = groups.keys().map(|k| k.0).min().unwrap_or(2);
    let first = &groups.iter().min_by_key(|g| (g.0 .0, -g.1 .1)).unwrap();
    let title = if groups.len() == 1 {
        format!("Cloudflare: {}", first.0 .1)
    } else {
        format!("{total} Cloudflare alerts today")
    };
    let urgent = rows.iter().any(|r| r["urgent"] == true);
    Some(json!({
        "id": "cloud:alerts",
        "kind": "cloud",
        "src": "cloudflare",
        "title": title,
        "short": format!("{total} alert{}", if total == 1 { "" } else { "s" }),
        "subtitle": format!("Latest at {}", clock(first.1 .1)),
        "rows": rows,
        "sections": ["DOWN", "WATCH", "FYI"],
        "priority": match worst { 0 => 70, 1 => 45, _ => 20 },
        "urgent": urgent,
        "start": now.to_rfc3339(),
        "end": (now + chrono::Duration::hours(6)).to_rfc3339(),
    }))
}

pub fn build(token: &str, account: &str, now: DateTime<Local>) -> Result<Option<Value>> {
    if token.trim().is_empty() || account.trim().is_empty() {
        return Ok(None);
    }
    Ok(card(&history(token, account, now)?, now))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn groups_and_ranks_alerts() {
        let now = Local::now();
        let t = |m: i64| (now - chrono::Duration::minutes(m)).to_rfc3339();
        let alerts = vec![
            json!({"alert_type": "workers_alert", "name": "api-worker errors", "sent": t(200)}),
            json!({"alert_type": "workers_alert", "name": "api-worker errors", "sent": t(100)}),
            json!({"alert_type": "dos_attack_l7", "name": "HTTP DDoS on example.com", "sent": t(20)}),
        ];
        let c = card(&alerts, now).unwrap();
        assert_eq!(c["title"], "3 Cloudflare alerts today");
        assert_eq!(c["rows"][0]["label"], "DDoS attack");
        assert_eq!(c["rows"][0]["urgent"], true);
        assert_eq!(c["rows"][1]["badge"], "2");
        assert_eq!(c["urgent"], true);
        assert!(card(&[], now).is_none());
    }

    #[test]
    fn traffic_card_compares_with_last_week() {
        let now = Local::now();
        let t = Traffic { zone: "example.com".into(), hourly: vec![100, 300, 900, 4000], today: 5300, last_week: 3200,
                          top_path: "/blog/launch".into(), top_referrer: "news.ycombinator.com".into() };
        let c = traffic_card(&t, now).unwrap();
        assert_eq!(c["big"], "5,300");
        assert_eq!(c["short"], "Traffic up 66%");
        assert!(c["delta"].as_str().unwrap().starts_with("+66% vs last "));
        assert_eq!(c["subtitle"], "/blog/launch  ·  most from Hacker News");
        assert_eq!(c["priority"], 42);
        let quiet = Traffic { today: 0, ..t };
        assert!(traffic_card(&quiet, now).is_none());
        assert_eq!(thousands(1234567), "1,234,567");
    }
}
