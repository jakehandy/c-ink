//! Settings persisted by C-Ink Settings (and read by the headless bridge).
//! Stored as JSON in the OS config dir: ~/Library/Application Support/C-Ink
//! on macOS, %APPDATA%\C-Ink on Windows, ~/.config/C-Ink on Linux.

use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};

use crate::kinds::KINDS;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    /// Explicit path to the `claude` binary; found automatically when empty.
    pub claude_path: String,
    /// Model alias passed to Claude Code for digest runs.
    pub model: String,
    /// Minutes between syncs (device and digest).
    pub refresh_min: u32,
    /// Minutes between Claude's smart check-ins after the morning plan.
    /// 0 = the morning plan only.
    pub smart_every_min: u32,
    /// Between check-ins, re-read the calendars every refresh (a small, fast run)
    /// so meeting times, new invites and cancellations show up right away.
    pub quick_calendar: bool,
    /// Bumped when a default changes, so old settings files can be migrated.
    /// Missing in files from before versioning, so it reads as 0 there.
    #[serde(default)]
    pub version: u32,
    /// Screen kind -> enabled.
    pub screens: BTreeMap<String, bool>,
    /// A sentence or two about the user, for Claude's judgement ("I run eng at...").
    pub about: String,
    /// People or topics Claude must never read, mention or show.
    pub private_terms: Vec<String>,
    /// People whose messages always matter.
    pub vips: Vec<String>,
    /// Free-form extra instructions for the digest.
    pub instructions: String,
    /// Quiet hours (start hour, end hour), local time: the badge shows the
    /// good night screen and nothing syncs. None = always on.
    pub quiet: Option<(u8, u8)>,
    /// Never use the panel's accent color.
    pub mono: bool,
    /// Turn the picture 180 degrees, for wearing the badge the other way up.
    pub flip: bool,
    /// LAN port for devices.
    pub port: u16,
    /// Optional relay so the TICKEY can sync away from home.
    pub relay: Option<Relay>,
    /// Paired devices.
    pub devices: Vec<Device>,
    /// Read-only tools found on the last connection check (exact names).
    pub tools: Vec<String>,
    /// Unix time of the last connection check.
    pub tools_checked: i64,
    /// Skip Claude runs during quiet hours.
    pub pause_in_quiet: bool,
    /// Usual starting point for commute estimates ("home in Oakland, near Lake Merritt").
    pub commute_from: String,
    /// "drive", "transit", "walk" or "bike".
    pub commute_mode: String,
    /// Minutes added to every commute estimate.
    pub commute_buffer_min: u32,
    /// Last board profile sent to a TICKEY (pins, panel scripts), so it can be
    /// re-sent after a firmware reflash. Null until one is sent.
    pub board_profile: serde_json::Value,
    /// Where the weather screen is for: "94110", "Portland, OR" or "45.52,-122.68".
    pub weather_location: String,
    /// The place that location resolved to, looked up once.
    pub weather_place: Option<crate::weather::Place>,
    pub weather_fahrenheit: bool,
    /// What the morning news screen should cover, in the owner's words.
    pub news_prompt: String,
    /// Read-only Cloudflare API token (Account > Notifications > Read).
    pub cloudflare_token: String,
    pub cloudflare_account: String,
    pub cloudflare_account_name: String,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Relay {
    pub url: String,
    /// Secret the bridge uses to upload (the relay's WRITE_TOKEN).
    pub write_token: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Device {
    pub id: String,
    pub name: String,
    /// base64 AES-256 key, shared only with this device over USB.
    pub key: String,
    /// Bearer token the device presents when fetching.
    pub token: String,
    pub firmware: String,
    pub paired_at: i64,
}

impl Default for Settings {
    fn default() -> Self {
        let screens = KINDS.iter().map(|k| (k.key.to_string(), k.default_on)).collect();
        Settings {
            claude_path: String::new(),
            model: "sonnet".into(),
            refresh_min: 15,
            smart_every_min: 60,
            quick_calendar: true,
            version: SETTINGS_VERSION,
            screens,
            about: String::new(),
            private_terms: vec![],
            vips: vec![],
            instructions: String::new(),
            quiet: Some((22, 7)),
            mono: false,
            flip: false,
            port: 7787,
            relay: None,
            devices: vec![],
            tools: vec![],
            tools_checked: 0,
            pause_in_quiet: true,
            commute_from: String::new(),
            commute_mode: "drive".into(),
            commute_buffer_min: 5,
            board_profile: serde_json::Value::Null,
            weather_location: String::new(),
            weather_place: None,
            weather_fahrenheit: true,
            news_prompt: String::new(),
            cloudflare_token: String::new(),
            cloudflare_account: String::new(),
            cloudflare_account_name: String::new(),
        }
    }
}

/// Version 2: quiet hours default to 10 PM - 7 AM (was 11 PM).
const SETTINGS_VERSION: u32 = 2;

impl Settings {
    /// Moves old defaults the owner never changed to the new ones.
    fn migrate(&mut self) {
        if self.version < 2 && self.quiet == Some((23, 7)) {
            self.quiet = Some((22, 7));
        }
        self.version = SETTINGS_VERSION;
    }

    pub fn dir() -> PathBuf {
        dirs::config_dir().unwrap_or_else(|| PathBuf::from(".")).join("C-Ink")
    }

    pub fn path() -> PathBuf {
        Self::dir().join("settings.json")
    }

    pub fn load() -> Settings {
        let p = Self::path();
        match fs::read_to_string(&p) {
            Ok(s) => {
                let mut v: Settings = serde_json::from_str(&s).unwrap_or_default();
                // New screen kinds added in an update default to their usual state.
                for k in KINDS {
                    v.screens.entry(k.key.to_string()).or_insert(k.default_on);
                }
                v.refresh_min = v.refresh_min.clamp(5, 240);
                v.migrate();
                v
            }
            Err(_) => Settings::default(),
        }
    }

    pub fn save(&self) -> Result<()> {
        fs::create_dir_all(Self::dir())?;
        let p = Self::path();
        let tmp = p.with_extension("json.tmp");
        fs::write(&tmp, serde_json::to_vec_pretty(self)?)?;
        restrict(&tmp);
        fs::rename(&tmp, &p).context("saving settings")?;
        Ok(())
    }

    pub fn enabled(&self, kind: &str) -> bool {
        *self.screens.get(kind).unwrap_or(&false)
    }

    /// Bitmask of disabled kinds, in device bit order.
    pub fn disabled_mask(&self) -> u32 {
        KINDS
            .iter()
            .enumerate()
            .filter(|(_, k)| !self.enabled(k.key))
            .fold(0, |m, (i, _)| m | (1 << i))
    }
}

/// Keys live in this file: keep it readable by the user only.
#[cfg(unix)]
fn restrict(p: &std::path::Path) {
    use std::os::unix::fs::PermissionsExt;
    let _ = fs::set_permissions(p, fs::Permissions::from_mode(0o600));
}
#[cfg(not(unix))]
fn restrict(_: &std::path::Path) {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn old_quiet_default_moves_to_ten() {
        let mut s: Settings = serde_json::from_str(r#"{"quiet": [23, 7]}"#).unwrap();
        assert_eq!(s.version, 0);
        assert_eq!(s.smart_every_min, 60);
        s.migrate();
        assert_eq!(s.quiet, Some((22, 7)));
        let mut mine: Settings = serde_json::from_str(r#"{"quiet": [21, 6], "version": 1}"#).unwrap();
        mine.migrate();
        assert_eq!(mine.quiet, Some((21, 6)));
    }
}
