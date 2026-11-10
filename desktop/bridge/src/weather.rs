//! Weather screen, built by the bridge itself from Open-Meteo (free, no
//! account). Only the place you set is sent to Open-Meteo; Claude is not
//! involved, so the forecast costs no Claude run.

use anyhow::{anyhow, bail, Context, Result};
use chrono::{DateTime, FixedOffset, Local, NaiveDateTime, TimeZone, Timelike};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Place {
    /// What the owner typed, so a changed location is looked up again.
    #[serde(default)]
    pub query: String,
    pub name: String,
    pub lat: f64,
    pub lon: f64,
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new().timeout(std::time::Duration::from_secs(15)).user_agent("C-Ink").build()
}

/// "94110", "Portland", "Portland, OR" or "45.52,-122.68" to a place.
pub fn geocode(q: &str) -> Result<Place> {
    let q = q.trim();
    if q.is_empty() {
        bail!("set a place first");
    }
    if let Some((a, b)) = q.split_once(',') {
        if let (Ok(lat), Ok(lon)) = (a.trim().parse::<f64>(), b.trim().parse::<f64>()) {
            return Ok(Place { query: q.into(), name: format!("{lat:.2}, {lon:.2}"), lat, lon });
        }
    }
    // Open-Meteo matches names or postal codes; "City, ST" works best as the city alone.
    let name = q.split(',').next().unwrap_or(q).trim();
    let mut req = agent().get("https://geocoding-api.open-meteo.com/v1/search").query("name", name).query("count", "5")
        .query("language", "en").query("format", "json");
    if name.len() == 5 && name.chars().all(|c| c.is_ascii_digit()) {
        req = req.query("countryCode", "US");
    }
    let v: Value = req.call().context("looking up the place")?.into_json()?;
    let results = v["results"].as_array().cloned().unwrap_or_default();
    // Prefer a result whose region matches what follows the comma ("TN", "Tennessee").
    let hint = q.split_once(',').map(|(_, h)| h.trim().to_lowercase()).unwrap_or_default();
    let pick = results
        .iter()
        .find(|r| !hint.is_empty() && {
            let a = r["admin1"].as_str().unwrap_or("").to_lowercase();
            a.starts_with(&hint) || state_code(&a) == hint
        })
        .or_else(|| results.first())
        .ok_or_else(|| anyhow!("no place called {q}"))?;
    let city = pick["name"].as_str().unwrap_or(name);
    let region = pick["admin1"].as_str().map(|a| {
        let c = state_code(&a.to_lowercase());
        if c.is_empty() { a.to_string() } else { c.to_uppercase() }
    });
    Ok(Place {
        query: q.into(),
        name: match region { Some(r) if !r.is_empty() => format!("{city}, {r}"), _ => city.to_string() },
        lat: pick["latitude"].as_f64().ok_or_else(|| anyhow!("bad geocoding answer"))?,
        lon: pick["longitude"].as_f64().ok_or_else(|| anyhow!("bad geocoding answer"))?,
    })
}

fn state_code(name: &str) -> String {
    const S: &[(&str, &str)] = &[("alabama", "al"), ("alaska", "ak"), ("arizona", "az"), ("arkansas", "ar"),
        ("california", "ca"), ("colorado", "co"), ("connecticut", "ct"), ("delaware", "de"), ("florida", "fl"),
        ("georgia", "ga"), ("hawaii", "hi"), ("idaho", "id"), ("illinois", "il"), ("indiana", "in"), ("iowa", "ia"),
        ("kansas", "ks"), ("kentucky", "ky"), ("louisiana", "la"), ("maine", "me"), ("maryland", "md"),
        ("massachusetts", "ma"), ("michigan", "mi"), ("minnesota", "mn"), ("mississippi", "ms"), ("missouri", "mo"),
        ("montana", "mt"), ("nebraska", "ne"), ("nevada", "nv"), ("new hampshire", "nh"), ("new jersey", "nj"),
        ("new mexico", "nm"), ("new york", "ny"), ("north carolina", "nc"), ("north dakota", "nd"), ("ohio", "oh"),
        ("oklahoma", "ok"), ("oregon", "or"), ("pennsylvania", "pa"), ("rhode island", "ri"),
        ("south carolina", "sc"), ("south dakota", "sd"), ("tennessee", "tn"), ("texas", "tx"), ("utah", "ut"),
        ("vermont", "vt"), ("virginia", "va"), ("washington", "wa"), ("west virginia", "wv"), ("wisconsin", "wi"),
        ("wyoming", "wy"), ("district of columbia", "dc")];
    S.iter().find(|(n, _)| *n == name).map(|(_, c)| c.to_string()).unwrap_or_default()
}

pub fn fetch(p: &Place, fahrenheit: bool) -> Result<Value> {
    let v: Value = agent()
        .get("https://api.open-meteo.com/v1/forecast")
        .query("latitude", &p.lat.to_string())
        .query("longitude", &p.lon.to_string())
        .query("current", "temperature_2m,apparent_temperature,weather_code")
        .query("hourly", "temperature_2m,precipitation_probability,weather_code")
        .query("daily", "temperature_2m_max,temperature_2m_min")
        .query("temperature_unit", if fahrenheit { "fahrenheit" } else { "celsius" })
        .query("timezone", "auto")
        .query("forecast_days", "2")
        .call()
        .context("fetching the forecast")?
        .into_json()?;
    Ok(v)
}

/// WMO weather code to the device's condition keys.
pub fn condition(code: i64) -> (&'static str, &'static str) {
    match code {
        0 => ("clear", "Clear"),
        1 | 2 => ("partly", "Partly cloudy"),
        3 => ("cloudy", "Cloudy"),
        45 | 48 => ("fog", "Fog"),
        51..=57 => ("rain", "Drizzle"),
        61..=67 | 80..=82 => ("rain", "Rain"),
        71..=77 | 85 | 86 => ("snow", "Snow"),
        95..=99 => ("storm", "Thunderstorms"),
        _ => ("cloudy", "Cloudy"),
    }
}

fn hour_label(t: &DateTime<FixedOffset>) -> String {
    let h = t.hour() % 12;
    format!("{} {}", if h == 0 { 12 } else { h }, if t.hour() < 12 { "AM" } else { "PM" })
}

/// Builds a card in the same shape Claude returns, so it goes through the
/// same trimming and privacy pass.
pub fn card(v: &Value, p: &Place, now: DateTime<Local>) -> Result<Value> {
    let off = FixedOffset::east_opt(v["utc_offset_seconds"].as_i64().unwrap_or(0) as i32).unwrap();
    let deg = if v["current_units"]["temperature_2m"].as_str().unwrap_or("").contains('F') { "°" } else { "°C" };
    let temp = v["current"]["temperature_2m"].as_f64().ok_or_else(|| anyhow!("no current temperature"))?;
    let feels = v["current"]["apparent_temperature"].as_f64().unwrap_or(temp);
    let (wx, wx_name) = condition(v["current"]["weather_code"].as_i64().unwrap_or(3));
    let hi = v["daily"]["temperature_2m_max"][0].as_f64().unwrap_or(temp);
    let lo = v["daily"]["temperature_2m_min"][0].as_f64().unwrap_or(temp);

    // The next 12 hours from now.
    let times = v["hourly"]["time"].as_array().cloned().unwrap_or_default();
    let mut next: Vec<(DateTime<FixedOffset>, f64, i64, i64)> = vec![];
    for (i, t) in times.iter().enumerate() {
        let Some(t) = t.as_str().and_then(|s| NaiveDateTime::parse_from_str(s, "%Y-%m-%dT%H:%M").ok()) else { continue };
        let Some(t) = off.from_local_datetime(&t).single() else { continue };
        if t.timestamp() + 3600 <= now.timestamp() {
            continue;
        }
        let h = &v["hourly"];
        next.push((t, h["temperature_2m"][i].as_f64().unwrap_or(temp), h["precipitation_probability"][i].as_i64().unwrap_or(0),
                   h["weather_code"][i].as_i64().unwrap_or(0)));
        if next.len() == 12 {
            break;
        }
    }
    let wet = |c: i64| matches!(condition(c).0, "rain" | "storm" | "snow");
    let rain_start = next.iter().find(|(_, _, pp, c)| *pp >= 50 || (wet(*c) && *pp >= 30));
    let storm = next.iter().any(|(_, _, pp, c)| condition(*c).0 == "storm" && *pp >= 40);
    let (title, body, priority) = match rain_start {
        Some((t, _, pp, c)) if t.timestamp() <= now.timestamp() + 1800 => {
            let what = condition(*c).1;
            let stop = next.iter().skip_while(|x| x.0 <= *t).find(|(_, _, pp2, c2)| *pp2 < 30 && !wet(*c2));
            let title = match stop {
                Some((s, ..)) => format!("{what} now, easing by {}", hour_label(s)),
                None => format!("{what} now and through the evening"),
            };
            (title, format!("{pp}% chance this hour. Take an umbrella."), if storm { 55 } else { 40 })
        }
        Some((t, _, pp, c)) => (
            format!("{} likely from {}", condition(*c).1, hour_label(t)),
            format!("{pp}% chance by then. Dry until around {}.", hour_label(t)),
            if storm { 50 } else { 35 },
        ),
        None => (
            format!("{} and dry, {}{deg} high", wx_name, hi.round()),
            "No rain in the next 12 hours.".to_string(),
            15,
        ),
    };
    let short = format!("{}{deg} {}", temp.round(), wx_name.to_lowercase());
    Ok(json!({
        "id": "weather:now",
        "kind": "weather",
        "src": "",
        "wx": wx,
        "title": title,
        "short": short,
        "big": format!("{}{deg}", temp.round()),
        "delta": format!("High {}{deg}  ·  Low {}{deg}", hi.round(), lo.round()),
        "subtitle": format!("Feels like {}{deg}  ·  {}", feels.round(), p.name),
        "body": body,
        "spark": next.iter().map(|x| x.1).collect::<Vec<_>>(),
        "priority": priority,
        "urgent": storm,
        "start": now.to_rfc3339(),
        "end": (now + chrono::Duration::hours(3)).to_rfc3339(),
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Value {
        // Shape of an Open-Meteo answer, trimmed: 12:00-23:00 local, rain at 15:00.
        let times: Vec<String> = (0..24).map(|h| format!("2026-11-10T{h:02}:00")).collect();
        let temps: Vec<f64> = (0..24).map(|h| 45.0 + h as f64 * 0.5).collect();
        let pp: Vec<i64> = (0..24).map(|h| if (15..19).contains(&h) { 70 } else { 10 }).collect();
        let codes: Vec<i64> = (0..24).map(|h| if (15..19).contains(&h) { 61 } else { 2 }).collect();
        json!({
            "utc_offset_seconds": -21600,
            "current_units": {"temperature_2m": "°F"},
            "current": {"temperature_2m": 54.4, "apparent_temperature": 50.2, "weather_code": 2},
            "hourly": {"time": times, "temperature_2m": temps, "precipitation_probability": pp, "weather_code": codes},
            "daily": {"temperature_2m_max": [61.2], "temperature_2m_min": [43.8]},
        })
    }

    #[test]
    fn builds_a_rain_card() {
        let now = DateTime::parse_from_rfc3339("2026-11-10T12:30:00-06:00").unwrap().with_timezone(&Local);
        let p = Place { query: "97201".into(), name: "Portland, OR".into(), lat: 45.5, lon: -122.7 };
        let c = card(&sample(), &p, now).unwrap();
        assert_eq!(c["wx"], "partly");
        assert_eq!(c["big"], "54°");
        assert_eq!(c["title"], "Rain likely from 3 PM");
        assert_eq!(c["delta"], "High 61°  ·  Low 44°");
        assert!(c["subtitle"].as_str().unwrap().ends_with("Portland, OR"));
        assert_eq!(c["spark"].as_array().unwrap().len(), 12);
    }

    #[test]
    fn codes() {
        assert_eq!(condition(0).0, "clear");
        assert_eq!(condition(95).0, "storm");
        assert_eq!(condition(73).0, "snow");
        assert_eq!(state_code("tennessee"), "tn");
    }
}
