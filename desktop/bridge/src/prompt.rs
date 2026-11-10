//! The digest prompt and its output schema. This is where "what matters right
//! now" is defined; the device only re-ranks what Claude returns as time passes.

use chrono::{DateTime, Local};

use crate::kinds::{BUILTIN, KINDS};
use crate::settings::Settings;

pub const SCHEMA: &str = r#"{
  "type": "object",
  "properties": {
    "headline": {"type": "string", "description": "One plain sentence on the shape of the day, max 60 chars. Shown when there is nothing else on the screen, so it can have a little personality."},
    "cards": {
      "type": "array",
      "maxItems": 20,
      "items": {
        "type": "object",
        "properties": {
          "id": {"type": "string", "description": "Stable id, e.g. cal:<event id>, gh:queue, slack:triage"},
          "kind": {"type": "string", "enum": ["event","review","chat","mail","task","ship","money","music","quest","focus","brief","doc","other","traffic","db","commute","wins","parcel"]},
          "src": {"type": "string", "enum": ["gcal","github","gmail","gdrive","linear","vercel","stripe","supabase","shop","notion","spotify","slack",""], "description": "Which service this came from, for its icon"},
          "sections": {"type": "array", "items": {"type": "string"}, "maxItems": 3, "description": "Names for row tiers 0, 1 and 2, max 12 chars each, when the default (NOW, TODAY, WHEN YOU CAN) does not fit"},
          "title": {"type": "string", "description": "Max 40 chars"},
          "short": {"type": "string", "description": "Max 22 chars, for the 'up next' list"},
          "subtitle": {"type": "string", "description": "Max 34 chars"},
          "body": {"type": "string", "description": "Max 150 chars. The one thing worth knowing."},
          "meta": {"type": "string", "description": "Max 40 chars, e.g. 'Google Meet  ·  30 min'"},
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
                "detail": {"type": "string", "description": "Max 44 chars"},
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

pub fn build(s: &Settings, now: DateTime<Local>, tools: &[String], gh_context: Option<&str>, builtin: &[serde_json::Value]) -> String {
    let enabled: Vec<String> = KINDS
        .iter()
        .filter(|k| s.enabled(k.key) && !BUILTIN.contains(&k.key))
        .map(|k| format!("- {} ({}): {}", k.key, k.name, k.blurb))
        .collect();
    let servers = connector_names(tools);
    let mut p = String::new();
    p.push_str(&format!(
        "You are the brain of C-Ink, a 3.7-inch e-ink badge worn in portrait (480 wide, 720 tall) that shows its \
         owner the single most relevant thing in their day, with Clawd, the Claude Code mascot, in the corner. It refreshes every {} minutes. Right now it is {} ({}).\n\n",
        s.refresh_min,
        now.format("%A %B %-d, %Y, %-I:%M %p"),
        now.format("UTC%:z")
    ));
    if !s.about.trim().is_empty() {
        p.push_str(&format!("About the owner: {}\n\n", s.about.trim()));
    }
    if s.enabled("commute") {
        let from = s.commute_from.trim();
        p.push_str(&format!(
            "For commute cards: the owner usually starts from {}, gets around {}, and wants a {}-minute buffer.\n\n",
            if from.is_empty() { "an unknown place (only make commute cards between two in-person meetings)" } else { from },
            match s.commute_mode.as_str() { "transit" => "by public transit", "walk" => "on foot", "bike" => "by bike", _ => "by car" },
            s.commute_buffer_min
        ));
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
    let inboxes = mail_accounts(tools);
    if s.enabled("mail") && inboxes > 1 {
        p.push_str(&format!(
            "The owner has {inboxes} email accounts connected (each mail connector is one account). Check every one.\n"
        ));
    }
    if !builtin.is_empty() {
        p.push_str("Already on the badge from C-Ink itself (do not make cards for these, but use them in the brief if they matter):\n");
        for c in builtin {
            p.push_str(&format!("- {}: {}\n", c["kind"].as_str().unwrap_or(""), c["title"].as_str().unwrap_or("")));
        }
    }
    p.push('\n');
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
- chat: ONE card titled "Slack", src "slack". Rows grouped by tier: 0 = needs the owner now (direct asks, mentions with deadlines, incidents they own), 1 = reply today, 2 = when they can. Each row is a person or channel plus the gist in a few words. Group related messages into one row with a count badge. Skip bots, joins, and anything already answered by the owner.
- mail: ONE card titled "Inbox", src "gmail", across every connected email account. Rows are threads where a real person is waiting on the owner: label = sender, detail = what they need. Ignore newsletters, receipts and notifications. With more than one account, use the row tier for the account (tier 0 = the first account, 1 = the second, 2 = the third) and set "sections" to short account names in that order (e.g. ["WORK", "PERSONAL", "SIDE"] from the address or domain, never a full address). With one account, use tiers for urgency as usual.
- task: issues assigned to the owner that are due within 2 days or that someone is blocked on. One card per issue (max 3) or one card with rows.
- ship: only failures that are still failing (latest production deploy errored, error spikes in the last hours). urgent = true. Nothing if all is green.
- money: one card, big = today's gross so far, delta = against the same weekday's usual, spark = cumulative revenue through today (up to 24 points, 0-100 scale). Only if the data is readily available.
- music: one card for the most recent release, big = streams this week (e.g. "12.4k"), delta vs last week, spark = daily trend.
- quest: the next scheduled tabletop session (title = campaign or adventure name, at = start) and in body where the owner's character stands (HP, notable state, last cliffhanger). Only if a session is within 7 days.
- focus: if there is a free block of 45+ minutes later today, one card for it: start = block start, end = block end, title like "Free until 4:30. Finish the rate limiter review." with the single highest-leverage item from everything above.
- brief: before 11 AM one morning brief card (start now, end 11 AM): the day in one line in title, body with the 2-3 things that matter. After 4 PM an evening wrap card (what got done, what tomorrow starts with).
- doc: docs shared with the owner or with unresolved comments for them, last 2 days.
- other: anything genuinely important that fits nowhere else.
- traffic: one card from Vercel Web Analytics for the owner's main site: big = visitors today so far (e.g. "8,312"), delta against the same weekday's usual, spark = hourly visitors today, subtitle = top page and the biggest referrer. Only when it is unusual (well above or below normal) or the owner launched something; priority 20-45.
- db: one card from Supabase advisors and logs: rows for security warnings (RLS off, exposed functions: tier 0, urgent if a table holding user data is public), performance warnings (tier 2), paused or unhealthy projects (tier 1). label = table or function, badge like "RLS" or "IDX". Nothing if clean.
- commute: for each in-person meeting in the next 12 hours that has a physical location (an address or a named place in the location field or invite, not a video link or a room in the owner's own office), one card. Work out where the owner will be right before it: the location of their previous in-person meeting that day if there is one, otherwise their usual starting point given below. Skip it if both are the same building. Estimate door-to-door time for the owner's usual way of getting around from what you know about the two places and the time of day (rush hour, a bridge, parking), round up to 5 minutes and add the owner's buffer. "at" = leave-by time (meeting start minus that estimate), "start" = 45 minutes before leave-by, "end" = the meeting start. title = "<mode verb> to <place>", e.g. "Drive to Blue Bottle Hayes Valley" (max 40). subtitle like "About 25 min by car" (say "about": it is an estimate without live traffic). body = the meeting it is for and its start time, plus one practical note if there is a real one (parking, which entrance, the train line). meta = the destination's street address, shortened. Priority 45-70. Never route to or from anything covered by the privacy rule.
- wins: Thursday afternoon onward, or any time the day is quiet, one card for the week so far: rows for merged PRs (count badge), Linear issues completed, deploys or releases that went live (ONCE, Vercel). Title with some warmth ("A good week so far"). Priority 25-40, higher on Friday afternoon. Never pad it: only real, completed work.
- parcel: ONE card titled "Deliveries" for packages arriving today or tomorrow, or delayed. Use the Shop connector's orders and tracking when it is connected, and order and shipping emails in the mail connectors (Shop, Link, Amazon, carriers). Rows: label = merchant (max 22), detail = item in a few words and status ("Arriving today by 8 PM", "Delayed, now Thursday"), badge "!" for a delay or a signature needed. Tiers: 0 = arriving today or needs action, 1 = tomorrow, 2 = in transit. sections ["TODAY", "TOMORROW", "ON THE WAY"]. src "shop" when any row came from Shop. Priority 25-45, higher on the day something arrives. Nothing if no package is moving.
- Do not make weather, claude, cloud or news cards: C-Ink builds those itself.

Writing rules: the screen is tiny, so write like a great chief of staff: specific, short, no filler, no emoji. Use absolute times ("since 2:27 PM"), never relative ones ("18 min ago"), because the card stays on screen for a while. Titles must make sense on their own. Use "urgent": true sparingly (an incident, a deadline within the hour, a failed production deploy). Priority 0-100 is how much it deserves the owner's attention compared to the other cards. All times are ISO 8601 with offset.
"#;

/// How many separate mail connectors (accounts) the tools come from.
pub fn mail_accounts(tools: &[String]) -> usize {
    let mut servers: Vec<&str> = tools
        .iter()
        .filter_map(|t| t.strip_prefix("mcp__"))
        .filter_map(|t| t.split("__").next())
        .filter(|s| { let l = s.to_lowercase(); l.contains("gmail") || l.contains("outlook") })
        .collect();
    servers.sort();
    servers.dedup();
    servers.len()
}

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
    fn counts_mail_accounts_and_skips_builtin_screens() {
        let t = vec!["mcp__Gmail__search_threads".to_string(), "mcp__Gmail_36b7b1a1__search_threads".to_string(),
                     "mcp__Gmail_36b7b1a1__get_thread".to_string(), "mcp__Slack__slack_read_channel".to_string()];
        assert_eq!(mail_accounts(&t), 2);
        let s = Settings::default();
        let w = serde_json::json!({"kind": "weather", "title": "Rain likely from 3 PM"});
        let p = build(&s, Local::now(), &t, None, &[w]);
        assert!(p.contains("2 email accounts") && p.contains("Rain likely from 3 PM"));
        assert!(!p.contains("- weather (Weather)"));
        assert!(p.contains("titled \"Slack\"") && !p.contains("Slack, sorted"));
    }

    #[test]
    fn privacy_terms_reach_prompt() {
        let mut s = Settings::default();
        s.private_terms = vec!["Project X".into()];
        let p = build(&s, Local::now(), &[], None, &[]);
        assert!(p.contains("PRIVACY RULE") && p.contains("Project X"));
    }
}
