//! Everything that talks to Claude Code. C-Ink never sees an API key: it runs
//! the user's own `claude` CLI, signed in with their Claude account, which
//! also brings the connectors they enabled on claude.ai.

use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context, Result};
use serde::Serialize;
use serde_json::Value;

#[derive(Clone, Debug, Default, Serialize)]
pub struct AuthStatus {
    pub installed: bool,
    pub path: String,
    pub version: String,
    pub logged_in: bool,
    pub method: String,
    pub detail: String,
}

/// Locates the claude binary: explicit setting, PATH, then the usual install
/// locations (GUI apps on macOS do not inherit the shell's PATH).
pub fn find(explicit: &str) -> Option<PathBuf> {
    if !explicit.is_empty() && Path::new(explicit).exists() {
        return Some(explicit.into());
    }
    if let Ok(p) = which::which("claude") {
        return Some(p);
    }
    let home = dirs::home_dir().unwrap_or_default();
    let mut c: Vec<PathBuf> = vec![
        home.join(".claude/local/claude"),
        home.join(".local/bin/claude"),
        "/opt/homebrew/bin/claude".into(),
        "/usr/local/bin/claude".into(),
        home.join(".npm-global/bin/claude"),
        home.join(".volta/bin/claude"),
    ];
    if cfg!(windows) {
        c.push(home.join(".local\\bin\\claude.exe"));
        c.push(home.join(".claude\\local\\claude.exe"));
        if let Some(appdata) = dirs::data_dir() {
            c.push(appdata.join("npm\\claude.cmd"));
        }
    }
    c.into_iter().find(|p| p.exists())
}

fn command(bin: &Path) -> Command {
    let mut c = Command::new(bin);
    // Windows: keep a console window from flashing up on every run.
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        c.creation_flags(0x0800_0000);
    }
    c.stdin(Stdio::null());
    c
}

/// Runs a command with a timeout, returning stdout.
fn run(mut cmd: Command, timeout: Duration) -> Result<String> {
    cmd.stdout(Stdio::piped()).stderr(Stdio::piped());
    let mut child = cmd.spawn().context("starting claude")?;
    let mut out = child.stdout.take().unwrap();
    let mut err = child.stderr.take().unwrap();
    let reader = std::thread::spawn(move || {
        let mut s = String::new();
        let _ = out.read_to_string(&mut s);
        s
    });
    let ereader = std::thread::spawn(move || {
        let mut s = String::new();
        let _ = err.read_to_string(&mut s);
        s
    });
    let start = Instant::now();
    loop {
        if let Some(status) = child.try_wait()? {
            let stdout = reader.join().unwrap_or_default();
            let stderr = ereader.join().unwrap_or_default();
            if !status.success() && stdout.trim().is_empty() {
                bail!("claude exited with {status}: {}", stderr.trim());
            }
            return Ok(stdout);
        }
        if start.elapsed() > timeout {
            let _ = child.kill();
            bail!("claude timed out after {}s", timeout.as_secs());
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

pub fn status(explicit: &str) -> AuthStatus {
    let Some(bin) = find(explicit) else {
        return AuthStatus { detail: "Claude Code is not installed".into(), ..Default::default() };
    };
    let mut st = AuthStatus { installed: true, path: bin.display().to_string(), ..Default::default() };
    let mut v = command(&bin);
    v.arg("--version");
    st.version = run(v, Duration::from_secs(15)).unwrap_or_default().trim().to_string();
    let mut c = command(&bin);
    c.args(["auth", "status", "--json"]);
    match run(c, Duration::from_secs(20)).ok().and_then(|s| serde_json::from_str::<Value>(&s).ok()) {
        Some(j) => {
            st.logged_in = j.get("loggedIn").and_then(Value::as_bool).unwrap_or(false);
            st.method = j.get("authMethod").and_then(Value::as_str).unwrap_or("").to_string();
            st.detail = if st.logged_in { "Signed in".into() } else { "Not signed in".into() };
        }
        None => st.detail = "Could not read sign-in status".into(),
    }
    st
}

/// Starts the browser sign-in flow (`claude auth login`). Returns immediately;
/// the UI polls `status` until it flips.
pub fn login(explicit: &str) -> Result<()> {
    let bin = find(explicit).ok_or_else(|| anyhow!("Claude Code is not installed"))?;
    let mut c = command(&bin);
    c.args(["auth", "login", "--claudeai"]).stdout(Stdio::null()).stderr(Stdio::null());
    c.spawn().context("starting claude auth login")?;
    Ok(())
}

/// Common flags for every headless run: print mode, structured output, no
/// local settings or code-running tools, nothing saved to session history,
/// and nothing allowed beyond the explicit tool list.
fn headless(bin: &Path, model: &str, prompt: &str, schema: &str, allowed: &[String]) -> Command {
    let mut c = command(bin);
    c.arg("-p")
        .arg(prompt)
        .args(["--output-format", "json"])
        .args(["--json-schema", schema])
        .args(["--model", model])
        .arg("--restricted")
        .args(["--permission-mode", "dontAsk"])
        .arg("--no-session-persistence");
    if !allowed.is_empty() {
        c.arg("--allowedTools").arg(allowed.join(","));
    }
    // A neutral working dir so no project CLAUDE.md leaks into the prompt.
    c.current_dir(crate::settings::Settings::dir());
    c
}

/// Pulls the structured result out of `--output-format json`.
fn structured(stdout: &str) -> Result<Value> {
    let j: Value = serde_json::from_str(stdout.trim()).context("claude did not return JSON")?;
    if j.get("is_error").and_then(Value::as_bool) == Some(true) {
        bail!("claude reported an error: {}", j.get("result").and_then(Value::as_str).unwrap_or("unknown"));
    }
    if let Some(v) = j.get("structured_output") {
        if !v.is_null() {
            return Ok(v.clone());
        }
    }
    // Fall back to JSON embedded in the text result.
    let text = j.get("result").and_then(Value::as_str).unwrap_or("");
    let start = text.find('{').ok_or_else(|| anyhow!("no JSON in claude's answer"))?;
    let end = text.rfind('}').ok_or_else(|| anyhow!("no JSON in claude's answer"))?;
    Ok(serde_json::from_str(&text[start..=end])?)
}

/// Asks Claude Code which MCP tools it has. No tool is called: the model only
/// reports the names in its context. The bridge keeps the read-only ones and
/// passes exactly those as the allow-list for digest runs.
pub fn discover_tools(explicit: &str) -> Result<Vec<String>> {
    let bin = find(explicit).ok_or_else(|| anyhow!("Claude Code is not installed"))?;
    let schema = r#"{"type":"object","properties":{"tools":{"type":"array","items":{"type":"string"}}},"required":["tools"]}"#;
    let prompt = "List the exact names of every MCP tool available to you (names that start with mcp__). \
                  Do not call any tool. Return them in the tools array.";
    let out = run(headless(&bin, "haiku", prompt, schema, &[]), Duration::from_secs(120))?;
    let v = structured(&out)?;
    let mut tools: Vec<String> = v["tools"]
        .as_array()
        .map(|a| a.iter().filter_map(|t| t.as_str().map(str::to_string)).collect())
        .unwrap_or_default();
    tools.retain(|t| t.starts_with("mcp__") && is_read_only(t));
    tools.sort();
    tools.dedup();
    Ok(tools)
}

/// Read-only by name. Digest runs may only call tools that pass this.
pub fn is_read_only(tool: &str) -> bool {
    let name = tool.rsplit("__").next().unwrap_or(tool).to_ascii_lowercase();
    let name = name.trim_start_matches("slack_");
    const READ: &[&str] = &[
        "list_", "get_", "search_", "read_", "query_", "fetch_", "find_", "count_", "aggregate_", "describe_",
    ];
    const EXACT: &[&str] = &["whoami", "get_me", "list_events", "pull_request_read", "issue_read", "actions_list"];
    const NEVER: &[&str] = &["send", "create", "update", "delete", "trash", "reply", "forward", "merge", "write",
        "post", "schedule", "upload", "share", "transfer", "deploy", "takedown", "submit", "respond", "mark_",
        "label", "set_", "save", "add_", "remove", "cancel", "execute", "apply", "run_", "edit", "invite", "pay"];
    if NEVER.iter().any(|w| name.contains(w)) {
        return false;
    }
    const SUFFIX: &[&str] = &["_read", "_list", "_search", "_get", "_details", "_summary", "_status", "_info"];
    EXACT.contains(&name) || READ.iter().any(|p| name.starts_with(p)) || SUFFIX.iter().any(|s| name.ends_with(s))
}

/// One digest run. Returns Claude's structured output (pre-normalization).
pub fn run_digest(explicit: &str, model: &str, prompt: &str, schema: &str, allowed: &[String]) -> Result<Value> {
    let bin = find(explicit).ok_or_else(|| anyhow!("Claude Code is not installed"))?;
    let out = run(headless(&bin, model, prompt, schema, allowed), Duration::from_secs(420))?;
    structured(&out)
}

/// The morning news run: web search and fetch only, and no connectors at all
/// (--strict-mcp-config with no config), so nothing read on the open web can
/// reach the owner's mail, calendar or anything else private.
pub fn run_web(explicit: &str, model: &str, prompt: &str, schema: &str) -> Result<Value> {
    let bin = find(explicit).ok_or_else(|| anyhow!("Claude Code is not installed"))?;
    let web = ["WebSearch".to_string(), "WebFetch".to_string()];
    let mut c = headless(&bin, model, prompt, schema, &web);
    c.args(["--tools", "WebSearch,WebFetch"]).arg("--strict-mcp-config");
    let out = run(c, Duration::from_secs(300))?;
    structured(&out)
}

fn find_gh() -> Option<PathBuf> {
    which::which("gh").ok().or_else(|| {
        ["/opt/homebrew/bin/gh", "/usr/local/bin/gh"].iter().map(PathBuf::from).find(|p| p.exists())
    })
}

/// True when the `gh` CLI is installed (the GitHub fallback is possible).
pub fn gh_context_available() -> bool {
    find_gh().is_some()
}

/// GitHub fallback when no GitHub connector is present: if the `gh` CLI is
/// installed and signed in, the bridge fetches PR lists itself and hands
/// them to Claude as context.
pub fn gh_context() -> Option<String> {
    let gh = find_gh()?;
    let q = |args: &[&str]| -> Option<String> {
        let mut c = command(&gh);
        c.args(args);
        run(c, Duration::from_secs(30)).ok().filter(|s| s.trim_start().starts_with('['))
    };
    let fields = "number,title,repository,url,updatedAt,isDraft,author";
    let review = q(&["search", "prs", "--review-requested=@me", "--state=open", "--json", fields, "--limit", "20"])?;
    let mine = q(&["search", "prs", "--author=@me", "--state=open", "--json", fields, "--limit", "20"]).unwrap_or("[]".into());
    Some(format!("PRs requesting my review: {review}\nMy open PRs: {mine}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn read_only_filter() {
        for t in ["mcp__claude_ai_Gmail__search_threads", "mcp__Slack__slack_read_channel", "mcp__x__list_events",
                  "mcp__github__pull_request_read", "mcp__Stripe__stripe_api_read", "mcp__Linear__get_issue"] {
            assert!(is_read_only(t), "{t}");
        }
        for t in ["mcp__Slack__slack_send_message", "mcp__Gmail__create_draft", "mcp__x__delete_event",
                  "mcp__github__merge_pull_request", "mcp__Linear__save_issue", "mcp__Vercel__create_deployment",
                  "mcp__Gmail__get_and_trash", "mcp__Supabase__execute_sql"] {
            assert!(!is_read_only(t), "{t}");
        }
    }

    #[test]
    fn structured_output_paths() {
        let v = structured(r#"{"type":"result","is_error":false,"structured_output":{"a":1}}"#).unwrap();
        assert_eq!(v["a"], 1);
        let v = structured(r#"{"is_error":false,"result":"Here you go: {\"a\":2}"}"#).unwrap();
        assert_eq!(v["a"], 2);
        assert!(structured(r#"{"is_error":true,"result":"rate limited"}"#).is_err());
    }
}
