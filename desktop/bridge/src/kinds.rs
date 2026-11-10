//! Screen kinds, in the device's bit order (must match cink::Kind).

pub struct KindInfo {
    pub key: &'static str,
    pub name: &'static str,
    pub blurb: &'static str,
    /// Lowercase fragments that identify the connector(s) this screen needs.
    /// Empty = derived from other sources (calendar) or always available.
    pub connectors: &'static [&'static str],
    pub default_on: bool,
}

pub const KINDS: &[KindInfo] = &[
    KindInfo { key: "event", name: "Next up", blurb: "Your next meetings with a countdown and the one thing to know going in.", connectors: &["calendar"], default_on: true },
    KindInfo { key: "review", name: "Pull requests", blurb: "PRs waiting on your review, and yours that are blocked or failing CI.", connectors: &["github"], default_on: true },
    KindInfo { key: "chat", name: "Slack", blurb: "Messages that need you, grouped into now, today and when you can.", connectors: &["slack"], default_on: true },
    KindInfo { key: "mail", name: "Inbox", blurb: "Email threads where someone is waiting on your reply.", connectors: &["gmail", "outlook", "mail"], default_on: true },
    KindInfo { key: "task", name: "Linear", blurb: "Issues due soon, or where you are blocking someone.", connectors: &["linear", "jira", "asana"], default_on: true },
    KindInfo { key: "ship", name: "Ship status", blurb: "Failed production deploys and fresh runtime errors.", connectors: &["vercel", "cloudflare", "netlify", "sentry"], default_on: true },
    KindInfo { key: "money", name: "Revenue", blurb: "Today's Stripe revenue against a normal day, with a sparkline.", connectors: &["stripe"], default_on: true },
    KindInfo { key: "music", name: "Releases", blurb: "How your latest music release is doing this week.", connectors: &["once", "spotify"], default_on: true },
    KindInfo { key: "quest", name: "Game night", blurb: "Your next tabletop session and where your character stands.", connectors: &["tavern"], default_on: true },
    KindInfo { key: "focus", name: "Focus window", blurb: "When you have a real gap, the single best thing to do with it.", connectors: &["calendar"], default_on: true },
    KindInfo { key: "brief", name: "Brief & wrap", blurb: "A one-card morning brief and an end-of-day wrap.", connectors: &[], default_on: true },
    KindInfo { key: "doc", name: "Docs", blurb: "Docs shared with you or waiting on your comments.", connectors: &["drive", "notion", "docs"], default_on: false },
    KindInfo { key: "other", name: "Anything else", blurb: "Something important that fits no other screen.", connectors: &[], default_on: true },
];

pub fn index_of(key: &str) -> Option<usize> {
    KINDS.iter().position(|k| k.key == key)
}
