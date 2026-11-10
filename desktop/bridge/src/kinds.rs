//! Screens and the sources that feed them.
//!
//! A *source* is something you connect in the C-Ink Marketplace (a claude.ai
//! connector, a local tool, or a built-in service). A *screen* is a card kind
//! the device can show full screen and as an "up next" row. Every screen can
//! be switched off on its own. Screens are listed in the device's bit order
//! (must match cink::Kind) and new ones are only ever appended.

pub struct KindInfo {
    pub key: &'static str,
    pub name: &'static str,
    pub blurb: &'static str,
    /// The source this screen lives under in the Marketplace.
    pub source: &'static str,
    /// Lowercase fragments that identify the connector(s) this screen needs.
    /// Empty = built in, local, or derived from other screens.
    pub connectors: &'static [&'static str],
    pub default_on: bool,
}

pub const KINDS: &[KindInfo] = &[
    KindInfo { key: "event", name: "Next up", blurb: "Your next meetings with a countdown and the one thing to know going in.", source: "gcal", connectors: &["calendar"], default_on: true },
    KindInfo { key: "review", name: "Pull requests", blurb: "PRs waiting on your review, and yours that are blocked or failing CI.", source: "github", connectors: &["github"], default_on: true },
    KindInfo { key: "chat", name: "Slack", blurb: "Messages that need you, grouped into now, today and when you can.", source: "slack", connectors: &["slack"], default_on: true },
    KindInfo { key: "mail", name: "Inbox", blurb: "Threads where someone is waiting on your reply, across every connected email account.", source: "gmail", connectors: &["gmail", "outlook", "mail"], default_on: true },
    KindInfo { key: "task", name: "Linear", blurb: "Issues due soon, or where you are blocking someone.", source: "linear", connectors: &["linear", "jira", "asana"], default_on: true },
    KindInfo { key: "ship", name: "Ship status", blurb: "Failed production deploys and fresh runtime errors.", source: "vercel", connectors: &["vercel", "netlify", "sentry"], default_on: true },
    KindInfo { key: "money", name: "Revenue", blurb: "Today's Stripe revenue against a normal day, with a sparkline.", source: "stripe", connectors: &["stripe"], default_on: true },
    KindInfo { key: "music", name: "Releases", blurb: "How your latest music release is doing this week.", source: "once", connectors: &["once", "spotify"], default_on: true },
    KindInfo { key: "quest", name: "Game night", blurb: "Your next tabletop session and where your character stands.", source: "tavern", connectors: &["tavern"], default_on: true },
    KindInfo { key: "focus", name: "Focus window", blurb: "When you have a real gap, the single best thing to do with it.", source: "gcal", connectors: &["calendar"], default_on: true },
    KindInfo { key: "brief", name: "Brief & wrap", blurb: "A one-card morning brief and an end-of-day wrap.", source: "claude", connectors: &[], default_on: true },
    KindInfo { key: "doc", name: "Docs", blurb: "Docs shared with you or waiting on your comments.", source: "gdrive", connectors: &["drive", "notion", "docs"], default_on: false },
    KindInfo { key: "other", name: "Anything else", blurb: "Something important that fits no other screen.", source: "claude", connectors: &[], default_on: true },
    // Appended after "other" so the device's bit order stays stable.
    KindInfo { key: "traffic", name: "Traffic", blurb: "Visitors today against a usual day, and where they came from.", source: "vercel", connectors: &["vercel"], default_on: true },
    KindInfo { key: "db", name: "Database health", blurb: "Supabase security and performance warnings, paused projects, failing functions.", source: "supabase", connectors: &["supabase"], default_on: true },
    KindInfo { key: "commute", name: "Commute", blurb: "When to leave for your next in-person meeting, from its location and where you will be before it.", source: "gcal", connectors: &["calendar"], default_on: true },
    KindInfo { key: "wins", name: "This week's wins", blurb: "What you shipped this week: merged PRs, closed issues, releases. Shows up on Friday afternoons and quiet stretches.", source: "github", connectors: &["github", "linear", "vercel", "once"], default_on: true },
    KindInfo { key: "news", name: "Morning news", blurb: "A morning briefing built from your own news prompt, researched on the web once a morning.", source: "news", connectors: &[], default_on: true },
    KindInfo { key: "weather", name: "Weather", blurb: "Now, today's high and low, and when rain starts, for the place you set.", source: "weather", connectors: &[], default_on: true },
    KindInfo { key: "claude", name: "Claude Code", blurb: "Sessions waiting on you, what you built today, and token use, from Claude Code on this computer.", source: "claudecode", connectors: &[], default_on: true },
    KindInfo { key: "cloud", name: "Cloudflare alerts", blurb: "Cloudflare notifications from the last day: Workers errors, origin and DDoS alerts, certificate expiry.", source: "cloudflare", connectors: &[], default_on: true },
    KindInfo { key: "parcel", name: "Deliveries", blurb: "Packages arriving today or delayed, from Shop and from Link's order emails.", source: "shop", connectors: &["shop", "gmail", "mail"], default_on: true },
];

/// Screens the bridge builds itself, so Claude is told not to make them.
pub const BUILTIN: &[&str] = &["weather", "claude", "cloud", "news"];

pub fn index_of(key: &str) -> Option<usize> {
    KINDS.iter().position(|k| k.key == key)
}

/// How a source gets connected.
pub enum Connect {
    /// A claude.ai connector; any of these fragments in a tool name counts.
    Connector(&'static [&'static str]),
    /// Configured right here in C-Ink Settings (a place, a prompt, a token).
    Builtin,
    /// Read from this computer.
    Local,
}

pub struct SourceInfo {
    pub id: &'static str,
    pub name: &'static str,
    /// Icon in desktop/ui/icons: a brand mark from tools/icongen.py or a line icon.
    pub icon: &'static str,
    pub blurb: &'static str,
    pub connect: Connect,
    /// Extra screens this source also feeds (it is listed under its own source).
    pub also: &'static [&'static str],
}

pub const SOURCES: &[SourceInfo] = &[
    SourceInfo { id: "gcal", name: "Google Calendar", icon: "gcal", blurb: "Meetings, free time and where you need to be.", connect: Connect::Connector(&["calendar"]), also: &[] },
    SourceInfo { id: "slack", name: "Slack", icon: "slack", blurb: "What needs you, sorted by how soon.", connect: Connect::Connector(&["slack"]), also: &[] },
    SourceInfo { id: "gmail", name: "Gmail", icon: "gmail", blurb: "Every connected account in one inbox screen. Add each account as its own Gmail connector.", connect: Connect::Connector(&["gmail", "outlook"]), also: &["parcel"] },
    SourceInfo { id: "github", name: "GitHub", icon: "github", blurb: "Reviews, failing CI and the week's merged work. The gh CLI works too.", connect: Connect::Connector(&["github"]), also: &[] },
    SourceInfo { id: "linear", name: "Linear", icon: "linear", blurb: "Issues due soon or blocking someone.", connect: Connect::Connector(&["linear"]), also: &["wins"] },
    SourceInfo { id: "vercel", name: "Vercel", icon: "vercel", blurb: "Failed deploys, runtime errors and traffic.", connect: Connect::Connector(&["vercel"]), also: &["wins"] },
    SourceInfo { id: "cloudflare", name: "Cloudflare", icon: "cloudflare", blurb: "Account alerts and Workers errors, with a read-only API token.", connect: Connect::Builtin, also: &[] },
    SourceInfo { id: "supabase", name: "Supabase", icon: "supabase", blurb: "Security and performance advisors.", connect: Connect::Connector(&["supabase"]), also: &[] },
    SourceInfo { id: "stripe", name: "Stripe", icon: "stripe", blurb: "Revenue today against a usual day.", connect: Connect::Connector(&["stripe"]), also: &[] },
    SourceInfo { id: "shop", name: "Shop", icon: "shopify", blurb: "Orders and deliveries tracked in the Shop app.", connect: Connect::Connector(&["shop"]), also: &[] },
    SourceInfo { id: "link", name: "Link", icon: "link", blurb: "Orders paid with Link. Link has no connector, so C-Ink reads Link's order and shipping emails in Gmail.", connect: Connect::Connector(&["gmail"]), also: &["parcel"] },
    SourceInfo { id: "once", name: "ONCE", icon: "once", blurb: "How your releases are doing.", connect: Connect::Connector(&["once"]), also: &["wins"] },
    SourceInfo { id: "tavern", name: "Tavern", icon: "tavern", blurb: "Your next session and your character.", connect: Connect::Connector(&["tavern"]), also: &[] },
    SourceInfo { id: "gdrive", name: "Google Drive", icon: "gdrive", blurb: "Docs waiting on your comments.", connect: Connect::Connector(&["drive", "notion"]), also: &[] },
    SourceInfo { id: "claudecode", name: "Claude Code", icon: "claude", blurb: "Your Claude Code sessions on this computer. Reads timing and token counts, never your conversations.", connect: Connect::Local, also: &[] },
    SourceInfo { id: "weather", name: "Weather", icon: "weather", blurb: "Forecast from Open-Meteo for the place you set. No account needed.", connect: Connect::Builtin, also: &[] },
    SourceInfo { id: "news", name: "News", icon: "news", blurb: "Claude researches the web once a morning with your prompt.", connect: Connect::Builtin, also: &[] },
    SourceInfo { id: "claude", name: "Claude", icon: "claude", blurb: "Built from everything above: the brief, the wrap and anything that fits nowhere else.", connect: Connect::Local, also: &[] },
];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_screen_has_a_source_and_fits_the_mask() {
        assert!(KINDS.len() <= 32);
        for k in KINDS {
            assert!(SOURCES.iter().any(|s| s.id == k.source), "{} has no source", k.key);
        }
        for s in SOURCES {
            for a in s.also {
                assert!(index_of(a).is_some(), "{} lists unknown screen {a}", s.id);
            }
        }
    }
}
