//! The digest prompt and its output schema. This is where "what matters right
//! now" is defined; the device only re-ranks what Claude returns as time passes.

use chrono::{DateTime, Local};

use crate::kinds::KINDS;
use crate::settings::Settings;

pub const SCHEMA: &str = r#"{
  "type": "object",
  "properties": {
    "headline": {"type": "string", "description": "One plain sentence on the shape of the day, max 70 chars"},
    "cards": {
      "type": "array",
      "maxItems": 20,
      "items": {
        "type": "object",
        "properties": {
          "id": {"type": "string", "description": "Stable id, e.g. cal:<event id>, gh:queue, slack:triage"},
          "kind": {"type": "string", "enum": ["event","review","chat","mail","task","ship","money","music","quest","focus","brief","doc","other"]},
          "title": {"type": "string", "description": "Max 44 chars"},
          "short": {"type": "string", "description": "Max 18 chars, for the small tiles"},
          "subtitle": {"type": "string", "description": "Max 60 chars"},
          "body": {"type": "string", "description": "Max 170 chars. The one thing worth knowing."},
          "meta": {"type": "string", "description": "Max 50 chars, e.g. 'Google Meet  ·  30 min'"},
          "big": {"type": "string", "description": "Headline number for money/music, e.g. '$4,820'"},
          "delta": {"type": "string", "description": "e.g. '+18% vs a usual Tuesday'"},
          "spark": {"type": "array", "items": {"type": "number"}, "maxItems": 24},
          "priority": {"type": "integer", "minimum": 0, "maximum": 100},
          "urgent": {"type": "boolean"},
          "at": {"type": "string", "description": "ISO 8601 start time for timed things (events, sessions)"},
          "start": {"type": "string", "description": "ISO 8601: when this card becomes relevant"},
          "end": {"type": "string", "description": "ISO 8601: when this card stops being relevant"},
          "rows": {
            "type": "array",
            "maxItems": 6,
            "items": {
              "type": "object",
              "properties": {
                "label": {"type": "string", "description": "Max 22 chars: person, channel or 'repo #123'"},
                "detail": {"type": "string", "description": "Max 70 chars"},
                "badge": {"type": "string", "description": "Max 3 chars: count, age like '2d', 'DM', 'CI', '!'"},
                "tier": {"type": "integer", "minimum": 0, "maximum": 2},
                "urgent": {"type": "boolean"}
              },
              "required": ["label"]
            }
          }
        },
        "required": ["id", "kind", "title", "priority"]
      }
    }
  },
  "required": ["headline", "cards"]
}"#;

pub fn build(s: &Settings, now: DateTime<Local>, tools: &[String], gh_context: Option<&str>) -> String {
    let enabled: Vec<String> = KINDS
        .iter()
        .filter(|k| s.enabled(k.key))
        .map(|k| format!("- {} ({}): {}", k.key, k.name, k.blurb))
        .collect();
    let servers = connector_names(tools);
    let mut p = String::new();
    p.push_str(&format!(
        "You are the brain of C-Ink, a 3.7-inch e-ink badge (720x480) that shows its owner the single most relevant \
         thing in their day. It refreshes every {} minutes. Right now it is {} ({}).\n\n",
        s.refresh_min,
        now.format("%A %B %-d, %Y, %-I:%M %p"),
        now.format("UTC%:z")
    ));
    if !s.about.trim().is_empty() {
        p.push_str(&format!("About the owner: {}\n\n", s.about.trim()));
    }
    p.push_str(
        "Use your connected tools (read-only) to look at what is happening from now until about 12 hours out, \
         plus the first thing tomorrow. Then return cards for the screens below. Be selective: an empty screen \
         is better than noise. Aim for 6 to 14 cards.\n\n",
    );
    p.push_str("Screens the owner has turned on:\n");
    p.push_str(&enabled.join("\n"));
    p.push_str(&format!("\n\nConnected services: {}.\n", if servers.is_empty() { "none detected".into() } else { servers.join(", ") }));
    p.push_str("Skip any screen whose service is not connected. Never invent data.\n\n");
    if let Some(gh) = gh_context {
        p.push_str("GitHub data fetched for you (use it for the review screen):\n");
        p.push_str(gh);
        p.push_str("\n\n");
    }
    p.push_str(GUIDE);
    if !s.vips.is_empty() {
        p.push_str(&format!("\nThese people always matter: anything from them ranks at least tier 1 / priority 60: {}.\n", s.vips.join(", ")));
    }
    if !s.private_terms.is_empty() {
        p.push_str(&format!(
            "\nPRIVACY RULE, no exceptions: never open, read, summarize or mention anything involving these people or \
             topics: {}. If a search result, event, message or email involves them, skip it without opening it and \
             leave it out entirely, even if it seems important.\n",
            s.private_terms.join(", ")
        ));
    }
    if !s.instructions.trim().is_empty() {
        p.push_str(&format!("\nOwner's own instructions: {}\n", s.instructions.trim()));
    }
    p
}

const GUIDE: &str = r#"How to build each screen:
- event: one card per meeting in the next 12 hours, plus the first meeting tomorrow. Skip declined events, cancelled events and all-day placeholders. "at" is the start, "end" the end. subtitle = who (first names, max 3), meta = where or how (room, Meet, Zoom) and length. body = the single thing worth knowing going in, pulled from the invite, a linked doc or a recent thread (what is being decided, what was promised). Priority 40-75 by importance; the device adds urgency as the start nears, so do not inflate it.
- review: ONE card. Rows = PRs waiting on the owner's review (tier 0 if older than a day or blocking a release, else 1) and the owner's own PRs that are failing CI or have changes requested (tier 0). label "repo #123", badge = age ("2d") or "CI". short like "3 PRs need you".
- chat: ONE card titled like "Slack, sorted". Rows grouped by tier: 0 = needs the owner now (direct asks, mentions with deadlines, incidents they own), 1 = reply today, 2 = when they can. Each row is a person or channel plus the gist in a few words. Group related messages into one row with a count badge. Skip bots, joins, and anything already answered by the owner.
- mail: ONE card with rows for threads where a real person is waiting on the owner. Ignore newsletters, receipts and notifications.
- task: issues assigned to the owner that are due within 2 days or that someone is blocked on. One card per issue (max 3) or one card with rows.
- ship: only failures that are still failing (latest production deploy errored, error spikes in the last hours). urgent = true. Nothing if all is green.
- money: one card, big = today's gross so far, delta = against the same weekday's usual, spark = cumulative revenue through today (up to 24 points, 0-100 scale). Only if the data is readily available.
- music: one card for the most recent release, big = streams this week (e.g. "12.4k"), delta vs last week, spark = daily trend.
- quest: the next scheduled tabletop session (title = campaign or adventure name, at = start) and in body where the owner's character stands (HP, notable state, last cliffhanger). Only if a session is within 7 days.
- focus: if there is a free block of 45+ minutes later today, one card for it: start = block start, end = block end, title like "Free until 4:30. Finish the rate limiter review." with the single highest-leverage item from everything above.
- brief: before 11 AM one morning brief card (start now, end 11 AM): the day in one line in title, body with the 2-3 things that matter. After 4 PM an evening wrap card (what got done, what tomorrow starts with).
- doc: docs shared with the owner or with unresolved comments for them, last 2 days.
- other: anything genuinely important that fits nowhere else.

Writing rules: the screen is tiny, so write like a great chief of staff: specific, short, no filler, no emoji. Use absolute times ("since 2:27 PM"), never relative ones ("18 min ago"), because the card stays on screen for a while. Titles must make sense on their own. Use "urgent": true sparingly (an incident, a deadline within the hour, a failed production deploy). Priority 0-100 is how much it deserves the owner's attention compared to the other cards. All times are ISO 8601 with offset.
"#;

/// Human-readable connector names from MCP tool names
/// (mcp__claude_ai_Google_Calendar__list_events -> "Google Calendar").
pub fn connector_names(tools: &[String]) -> Vec<String> {
    let mut names: Vec<String> = tools
        .iter()
        .filter_map(|t| t.strip_prefix("mcp__"))
        .filter_map(|t| t.split("__").next())
        .map(|s| {
            let s = s.strip_prefix("claude_ai_").unwrap_or(s);
            // Drop the random suffix duplicate connectors get (Gmail_36b7b1a1).
            let s = match s.rsplit_once('_') {
                Some((head, tail)) if tail.len() == 8 && tail.chars().all(|c| c.is_ascii_hexdigit()) => head,
                _ => s,
            };
            s.replace('_', " ")
        })
        .collect();
    names.sort();
    names.dedup();
    names
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn schema_is_json() {
        let _: serde_json::Value = serde_json::from_str(SCHEMA).unwrap();
    }

    #[test]
    fn connector_name_cleanup() {
        let t = vec![
            "mcp__claude_ai_Google_Calendar__list_events".to_string(),
            "mcp__Gmail_36b7b1a1__search_threads".to_string(),
            "mcp__Gmail__get_thread".to_string(),
            "mcp__Slack__slack_read_channel".to_string(),
        ];
        assert_eq!(connector_names(&t), vec!["Gmail", "Google Calendar", "Slack"]);
    }

    #[test]
    fn privacy_terms_reach_prompt() {
        let mut s = Settings::default();
        s.private_terms = vec!["Project X".into()];
        let p = build(&s, Local::now(), &[], None);
        assert!(p.contains("PRIVACY RULE") && p.contains("Project X"));
    }
}
