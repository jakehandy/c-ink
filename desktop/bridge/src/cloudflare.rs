//! Cloudflare alerts screen, built by the bridge from Cloudflare's own
//! notification history with a read-only API token. Claude is not involved.
//!
//! Token permissions needed: Account > Notifications > Read (and Account
//! Settings > Read so the account can be found automatically).

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
}
