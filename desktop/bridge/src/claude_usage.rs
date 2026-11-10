//! Claude Code screen, built by the bridge from the session logs Claude Code
//! keeps on this computer (~/.claude/projects/<project>/<session>.jsonl).
//!
//! Only metadata is read: timestamps, stop reasons, token counts, the working
//! directory and git branch. Message text is never parsed into anything, and
//! nothing here is sent to Claude: the card goes straight to the device.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::fs;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::Command;

use chrono::{DateTime, Local, TimeZone, Timelike};
use serde::Deserialize;
use serde_json::{json, Value};

#[derive(Deserialize)]
struct Line {
    #[serde(rename = "type", default)]
    kind: String,
    #[serde(default)]
    timestamp: String,
    #[serde(default)]
    cwd: String,
    #[serde(rename = "gitBranch", default)]
    branch: String,
    #[serde(rename = "isSidechain", default)]
    sidechain: bool,
    #[serde(default)]
    message: Option<Msg>,
}

#[derive(Deserialize)]
struct Msg {
    #[serde(default)]
    id: String,
    #[serde(default)]
    stop_reason: Option<String>,
    #[serde(default)]
    usage: Option<Usage>,
}

#[derive(Deserialize, Default, Clone, Copy)]
struct Usage {
    #[serde(default)]
    input_tokens: u64,
    #[serde(default)]
    output_tokens: u64,
    #[serde(default)]
    cache_creation_input_tokens: u64,
    #[serde(default)]
    cache_read_input_tokens: u64,
}

#[derive(Debug, Clone, PartialEq)]
pub enum State {
    /// Stopped on a tool call for a while: most likely a permission prompt.
    NeedsOk,
    /// Finished its turn recently: your move.
    YourTurn,
    /// Wrote something in the last couple of minutes.
    Working,
    Idle,
}

#[derive(Debug, Clone)]
pub struct Session {
    pub project: String,
    pub cwd: String,
    pub branch: String,
    pub last: i64,
    pub state: State,
}

#[derive(Debug, Default)]
pub struct Usage24 {
    pub sessions: Vec<Session>,
    /// Tokens today: input + output + cache writes + cache reads.
    pub total: u64,
    pub output: u64,
    /// Total tokens per hour of today, local time.
    pub hourly: [u64; 24],
}

pub fn projects_dir() -> PathBuf {
    std::env::var_os("CLAUDE_CONFIG_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| dirs::home_dir().unwrap_or_default().join(".claude"))
        .join("projects")
}

fn ts(s: &str) -> Option<i64> {
    DateTime::parse_from_rfc3339(s).ok().map(|d| d.timestamp())
}

/// Reads every session file touched in the last 24 hours.
pub fn scan(dir: &Path, now: DateTime<Local>) -> Usage24 {
    let mut u = Usage24::default();
    let midnight = Local
        .from_local_datetime(&now.date_naive().and_hms_opt(0, 0, 0).unwrap())
        .earliest()
        .map(|d| d.timestamp())
        .unwrap_or(now.timestamp() - 86400);
    let cutoff = std::time::SystemTime::now() - std::time::Duration::from_secs(24 * 3600);
    let mut seen: HashSet<String> = HashSet::new();
    let Ok(projects) = fs::read_dir(dir) else { return u };
    for p in projects.flatten() {
        let Ok(files) = fs::read_dir(p.path()) else { continue };
        for f in files.flatten() {
            let path = f.path();
            if path.extension().and_then(|e| e.to_str()) != Some("jsonl") {
                continue;
            }
            let fresh = f.metadata().and_then(|m| m.modified()).map(|t| t >= cutoff).unwrap_or(false);
            if !fresh {
                continue;
            }
            if let Some(s) = read_session(&path, midnight, &mut seen, &mut u) {
                u.sessions.push(s);
            }
        }
    }
    let now_s = now.timestamp();
    for s in &mut u.sessions {
        s.state = classify(&s.state, now_s - s.last);
    }
    u.sessions.sort_by_key(|s| std::cmp::Reverse(s.last));
    u
}

/// `raw` is the session's last stop: NeedsOk for a tool call, YourTurn for an
/// end of turn, Working for anything else (a user message, a tool result).
fn classify(raw: &State, age: i64) -> State {
    match raw {
        _ if age < 0 => State::Working,
        State::NeedsOk if (90..=3600).contains(&age) => State::NeedsOk,
        State::YourTurn if age <= 3600 => State::YourTurn,
        _ if age < 120 => State::Working,
        _ => State::Idle,
    }
}

fn read_session(path: &Path, midnight: i64, seen: &mut HashSet<String>, u: &mut Usage24) -> Option<Session> {
    let f = fs::File::open(path).ok()?;
    let mut s = Session { project: String::new(), cwd: String::new(), branch: String::new(), last: 0, state: State::Idle };
    for line in BufReader::new(f).lines().map_while(Result::ok) {
        let Ok(l) = serde_json::from_str::<Line>(&line) else { continue };
        if l.kind != "user" && l.kind != "assistant" {
            continue;
        }
        let Some(t) = ts(&l.timestamp) else { continue };
        if !l.cwd.is_empty() {
            s.cwd = l.cwd.clone();
        }
        if !l.branch.is_empty() {
            s.branch = l.branch.clone();
        }
        if let Some(m) = &l.message {
            if let Some(us) = m.usage {
                // Streaming writes one line per content block with the same id.
                if t >= midnight && (m.id.is_empty() || seen.insert(m.id.clone())) {
                    let total = us.input_tokens + us.output_tokens + us.cache_creation_input_tokens + us.cache_read_input_tokens;
                    u.total += total;
                    u.output += us.output_tokens;
                    if let Some(h) = Local.timestamp_opt(t, 0).single().map(|d| d.hour() as usize) {
                        u.hourly[h] += total;
                    }
                }
            }
        }
        if l.sidechain {
            continue; // subagent traffic does not change whether the session waits on you
        }
        if t >= s.last {
            s.last = t;
            s.state = match (l.kind.as_str(), l.message.as_ref().and_then(|m| m.stop_reason.as_deref())) {
                ("assistant", Some("tool_use")) => State::NeedsOk,
                ("assistant", Some("end_turn")) => State::YourTurn,
                _ => State::Working,
            };
        }
    }
    if s.last == 0 {
        return None;
    }
    s.project = Path::new(&s.cwd).file_name().and_then(|n| n.to_str()).unwrap_or("session").to_string();
    Some(s)
}

/// Commits made today in a repo: (count, latest subject).
fn commits_today(cwd: &str) -> Option<(usize, String)> {
    if cwd.is_empty() || !Path::new(cwd).join(".git").exists() {
        return None;
    }
    let mut c = Command::new("git");
    c.args(["-C", cwd, "log", "--since=midnight", "--no-merges", "--format=%s"]);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        c.creation_flags(0x0800_0000);
    }
    let out = c.output().ok().filter(|o| o.status.success())?;
    let text = String::from_utf8_lossy(&out.stdout);
    let subjects: Vec<&str> = text.lines().filter(|l| !l.trim().is_empty()).collect();
    if subjects.is_empty() {
        return None;
    }
    Some((subjects.len(), subjects[0].to_string()))
}

pub fn fmt_tokens(n: u64) -> String {
    match n {
        n if n >= 10_000_000 => format!("{:.0}M", n as f64 / 1e6),
        n if n >= 1_000_000 => format!("{:.1}M", n as f64 / 1e6),
        n if n >= 10_000 => format!("{:.0}k", n as f64 / 1e3),
        n if n >= 1_000 => format!("{:.1}k", n as f64 / 1e3),
        n => n.to_string(),
    }
}

fn clock(t: i64) -> String {
    Local.timestamp_opt(t, 0).single().map(|d| d.format("%-I:%M %p").to_string()).unwrap_or_default()
}

/// The Claude Code card, or None when Claude Code has not run today.
pub fn card(u: &Usage24, now: DateTime<Local>, commits: &dyn Fn(&str) -> Option<(usize, String)>) -> Option<Value> {
    if u.sessions.is_empty() && u.total == 0 {
        return None;
    }
    // One row per project: the most pressing session in it.
    let mut by_project: BTreeMap<String, &Session> = BTreeMap::new();
    for s in &u.sessions {
        let rank = |s: &Session| match s.state { State::NeedsOk => 0, State::YourTurn => 1, State::Working => 2, State::Idle => 3 };
        by_project
            .entry(s.project.clone())
            .and_modify(|e| if rank(s) < rank(e) { *e = s })
            .or_insert(s);
    }
    let mut built: HashMap<String, (usize, String)> = HashMap::new();
    let mut cwds: Vec<&str> = u.sessions.iter().map(|s| s.cwd.as_str()).collect();
    cwds.sort();
    cwds.dedup();
    for c in cwds {
        if let Some(b) = commits(c) {
            let name = Path::new(c).file_name().and_then(|n| n.to_str()).unwrap_or(c).to_string();
            let e = built.entry(name).or_insert((0, String::new()));
            e.0 += b.0;
            if e.1.is_empty() {
                e.1 = b.1;
            }
        }
    }
    let total_commits: usize = built.values().map(|b| b.0).sum();

    let mut rows = vec![];
    let mut waiting = 0;
    let mut sessions: Vec<&&Session> = by_project.values().collect();
    sessions.sort_by_key(|s| (match s.state { State::NeedsOk => 0, State::YourTurn => 1, State::Working => 2, State::Idle => 3 }, -s.last));
    for s in sessions.iter().take(6) {
        let b = built.get(&s.project);
        let (detail, tier, badge) = match s.state {
            State::NeedsOk => {
                waiting += 1;
                (format!("Waiting on your OK since {}", clock(s.last)), 0, "OK".to_string())
            }
            State::YourTurn => {
                waiting += 1;
                (format!("Finished at {}, your turn", clock(s.last)), 1, String::new())
            }
            State::Working => ("Working now".to_string(), 2, String::new()),
            State::Idle => match b {
                Some((_, subj)) => (subj.clone(), 2, String::new()),
                None => (format!("Last active {}", clock(s.last)), 2, String::new()),
            },
        };
        let badge = if badge.is_empty() { b.map(|b| b.0.to_string()).unwrap_or_default() } else { badge };
        let label = if s.branch.is_empty() || s.branch == "main" || s.branch == "master" {
            s.project.clone()
        } else {
            format!("{} {}", s.project, s.branch.rsplit('/').next().unwrap_or(&s.branch))
        };
        rows.push(json!({"label": label, "detail": detail, "tier": tier, "badge": badge}));
    }
    let needs_ok = sessions.iter().filter(|s| s.state == State::NeedsOk).count();
    let spark: Vec<u64> = u.hourly[..=(now.hour() as usize)].to_vec();
    let tokens = fmt_tokens(u.total);
    let built_line = match (total_commits, built.len()) {
        (0, _) => String::new(),
        (n, 1) => format!("{n} commit{} today", if n == 1 { "" } else { "s" }),
        (n, p) => format!("{n} commits across {p} projects today"),
    };
    let mut c = json!({
        "id": "claude:code",
        "kind": "claude",
        "src": "claude",
        "start": now.to_rfc3339(),
        "end": (now + chrono::Duration::hours(2)).to_rfc3339(),
        "meta": format!("{tokens} tokens today"),
        "rows": rows,
    });
    let o = c.as_object_mut().unwrap();
    if waiting > 0 {
        // A list card: the device shows rows when there is no big number.
        o.insert("title".into(), json!(match (needs_ok, waiting) {
            (1, 1) => "A Claude Code session needs your OK".to_string(),
            (0, 1) => "Claude Code finished, your turn".to_string(),
            (_, n) => format!("{n} Claude Code sessions waiting on you"),
        }));
        o.insert("short".into(), json!(if needs_ok > 0 { format!("{waiting} waiting, {needs_ok} need OK") } else { format!("{waiting} waiting on you") }));
        o.insert("subtitle".into(), json!(if built_line.is_empty() { format!("{tokens} tokens today") } else { built_line.clone() }));
        o.insert("priority".into(), json!(if needs_ok > 0 { 62 } else { 45 }));
        o.insert("sections".into(), json!(["NEEDS YOU", "YOUR TURN", "TODAY"]));
    } else {
        o.insert("title".into(), json!(if built_line.is_empty() { "Claude Code today".to_string() } else { built_line.clone() }));
        o.insert("short".into(), json!(format!("{tokens} tokens today")));
        o.insert("big".into(), json!(tokens));
        o.insert("delta".into(), json!(format!("{} written by Claude", fmt_tokens(u.output))));
        o.insert("subtitle".into(), json!(match sessions.first() {
            Some(s) if s.state == State::Working => format!("Working in {} now", s.project),
            Some(s) => format!("Last session in {} at {}", s.project, clock(s.last)),
            None => String::new(),
        }));
        o.insert("body".into(), json!(built.iter().max_by_key(|b| b.1 .0).map(|(p, b)| format!("Latest in {p}: {}", b.1)).unwrap_or_default()));
        o.insert("spark".into(), json!(spark));
        o.insert("priority".into(), json!(if sessions.first().map(|s| s.state == State::Working).unwrap_or(false) { 30 } else { 20 }));
    }
    Some(c)
}

/// Scans this computer and builds the card.
pub fn build(now: DateTime<Local>) -> Option<Value> {
    let u = scan(&projects_dir(), now);
    card(&u, now, &commits_today)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn line(kind: &str, t: &str, stop: Option<&str>, id: &str, out: u64) -> String {
        json!({"type": kind, "timestamp": t, "cwd": "/home/me/c-ink", "gitBranch": "feat/weather", "sessionId": "s",
               "message": {"id": id, "role": kind, "stop_reason": stop, "content": [{"type": "text", "text": "SECRET"}],
                           "usage": {"input_tokens": 100, "output_tokens": out, "cache_read_input_tokens": 1000}}})
            .to_string()
    }

    #[test]
    fn waiting_session_and_token_dedup() {
        let dir = std::env::temp_dir().join(format!("cink-cc-{}", std::process::id()));
        let proj = dir.join("-home-me-c-ink");
        fs::create_dir_all(&proj).unwrap();
        let now = Local::now();
        let t = |m: i64| (now - chrono::Duration::minutes(m)).to_rfc3339();
        let body = [
            line("user", &t(20), None, "", 0),
            line("assistant", &t(10), Some("tool_use"), "m1", 50),
            line("assistant", &t(10), Some("tool_use"), "m1", 50), // streamed duplicate
        ]
        .join("\n");
        fs::write(proj.join("abc.jsonl"), body).unwrap();
        let u = scan(&dir, now);
        assert_eq!(u.sessions.len(), 1);
        assert_eq!(u.sessions[0].state, State::NeedsOk);
        assert_eq!(u.sessions[0].project, "c-ink");
        // 2 user-line usages are not counted (no id dedupe needed), m1 counted once.
        assert!(u.output == 50, "{}", u.output);
        let c = card(&u, now, &|_| Some((3, "Add weather".into()))).unwrap();
        assert_eq!(c["title"], "A Claude Code session needs your OK");
        assert_eq!(c["rows"][0]["tier"], 0);
        assert_eq!(c["rows"][0]["label"], "c-ink weather");
        assert!(c.get("big").is_none());
        assert!(!c.to_string().contains("SECRET"), "message text must never reach the card");
        fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn states() {
        assert_eq!(classify(&State::NeedsOk, 30), State::Working);
        assert_eq!(classify(&State::NeedsOk, 600), State::NeedsOk);
        assert_eq!(classify(&State::YourTurn, 600), State::YourTurn);
        assert_eq!(classify(&State::YourTurn, 7200), State::Idle);
        assert_eq!(fmt_tokens(4_120_000), "4.1M");
        assert_eq!(fmt_tokens(38_400), "38k");
    }
}
