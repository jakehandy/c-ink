//! The digest prompt and its output schema. This is where "what matters right
//! now" is defined; the device only re-ranks what Claude returns as time passes.

use chrono::{DateTime, Local};

use crate::kinds::{BUILTIN, KINDS};
use crate::settings::Settings;

pub const SCHEMA: &str = r#"{
  "type": "object",
  "properties": {
    "headline": {"type": "string", "description": "One plain sentence on the shape of the day, max 60 chars. Shown when there is nothing else on the screen, so it can have a little personality."},
    "plan": {"type": "array", "items": {"type": "string"}, "maxItems": 10, "description": "Your plan for the rest of the day, for your own later check-ins (the owner never sees it): one line each, in time order, with what to watch for, e.g. '2:30 PM design review: Priya wants a yes/no on pricing step; brief from the Q3 pricing doc'"},
    "cards": {
      "type": "array",
      "maxItems": 20,
      "items": {
        "type": "object",
        "properties": {
          "id": {"type": "string", "description": "Stable id, e.g. cal:<event id>, gh:queue, slack:triage"},
          "kind": {"type": "string", "enum": ["event","review","chat","mail","task","ship","money","music","quest","focus","brief","doc","other","traffic","db","commute","wins","parcel","support","teams"]},
          "src": {"type": "string", "enum": ["gcal","github","gmail","gdrive","linear","vercel","stripe","supabase","shop","notion","spotify","slack","outlook","teams","m365","intercom","once","tavern",""], "description": "Which service this came from, for its icon"},
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

/// Which kind of smart run this is.
pub enum Pass<'a> {
    /// First run of the day: look at everything and plan the day.
    Morning,
    /// A later check-in: adjust this morning's plan and the cards on the badge.
    CheckIn { plan: &'a [String], current: &'a [serde_json::Value], since: DateTime<Local> },
}

pub fn build(s: &Settings, now: DateTime<Local>, tools: &[String], gh_context: Option<&str>, builtin: &[serde_json::Value]) -> String {
    build_pass(s, now, tools, gh_context, builtin, &Pass::Morning)
}

pub fn build_pass(s: &Settings, now: DateTime<Local>, tools: &[String], gh_context: Option<&str>, builtin: &[serde_json::Value],
                  pass: &Pass) -> String {
    let enabled: Vec<String> = KINDS
        .iter()
        .filter(|k| s.enabled(k.key) && !BUILTIN.contains(&k.key))
        .map(|k| format!("- {} ({}): {}", k.key, k.name, k.blurb))
        .collect();
    let servers = connector_names(tools);
    let mut p = String::new();
    p.push_str(&format!(
        "You are the brain of C-Ink, a 3.7-inch e-ink badge worn in portrait (480 wide, 720 tall) that shows its \
         owner the single most relevant thing in their day, with Clawd, the Claude Code mascot, in the corner. The badge \
         refreshes every {} minutes; between your runs C-Ink keeps calendar times current on its own. Right now it is {} ({}).\n\n",
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
    match pass {
        Pass::Morning => p.push_str(
            "This is the MORNING PLAN, the first run of the day. Use your connected tools (read-only) to look at the \
             whole day ahead, from now until about 12 hours out, plus the first thing tomorrow. Do the homework a great \
             chief of staff would: for every meeting, read the invite and anything linked or attached, search the docs \
             connectors (Drive, SharePoint, OneDrive, Notion) for the doc it is about, and check recent email, Slack and \
             Teams threads with the attendees and any related issues or PRs, so each event card says the one thing to know \
             going in. Then return cards for the screens below, and in \"plan\" write down the plan for the day that your \
             later check-ins will follow. Be selective: an empty screen is better than noise. Aim for 6 to 14 cards.\n\n",
        ),
        Pass::CheckIn { plan, current, since } => {
            p.push_str(&format!(
                "This is a CHECK-IN. You planned the day this morning and last looked at {}. Look for what is new or \
                 changed since then (new messages, moved or cancelled meetings, deploys, deliveries) and adjust: keep \
                 cards that still hold with the same id and wording, rewrite the ones that changed, drop what is done, add \
                 what is new, and research context for any meeting that appeared since the plan. Return the full set of \
                 cards, plus the plan updated for the rest of the day. Aim for 6 to 14 cards.\n\n",
                since.format("%-I:%M %p")
            ));
            if !plan.is_empty() {
                p.push_str("This morning's plan:\n");
                for l in plan.iter() {
                    p.push_str(&format!("- {l}\n"));
                }
                p.push('\n');
            }
            if !current.is_empty() {
                p.push_str("Cards on the badge now (id, kind, title):\n");
                for c in current.iter() {
                    p.push_str(&format!("- {} | {} | {}\n", c["id"].as_str().unwrap_or(""), c["kind"].as_str().unwrap_or(""),
                                        c["title"].as_str().unwrap_or("")));
                }
                p.push('\n');
            }
        }
    }
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
        p.push_str("Already on the badge from C-Ink itself (do not make another card for the same thing, but use them in the brief if they matter):\n");
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
- event: one card per meeting in the next 12 hours, plus the first meeting tomorrow, across every calendar connector (Google Calendar accounts and Outlook). id = "cal:" + the event id exactly as the connector gives it, so C-Ink can keep its time current between your runs. src "outlook" for Outlook events. Skip declined events, cancelled events and all-day placeholders. "at" is the start, "end" the end. subtitle = who (first names, max 3), meta = where or how (room, Meet, Zoom) and length. body = the single thing worth knowing going in, pulled from the invite, a linked doc or a recent thread (what is being decided, what was promised). Priority 40-75 by importance; the device adds urgency as the start nears, so do not inflate it.
- review: ONE card. Rows = PRs waiting on the owner's review (tier 0 if older than a day or blocking a release, else 1) and the owner's own PRs that are failing CI or have changes requested (tier 0). label "repo #123", badge = age ("2d") or "CI". short like "3 PRs need you".
- chat: ONE card, src "slack". The eyebrow already names the app, so the title is the unread count, "<N> unread" (e.g. "5 unread", "1 unread"), where N counts the unread DMs, mentions and threads waiting on the owner; if nothing is unread but rows remain, "<N> waiting" with N = the rows. short = "<N> unread in Slack". Rows grouped by tier: 0 = needs the owner now (direct asks, mentions with deadlines, incidents they own), 1 = reply today, 2 = when they can. Each row is a person or channel plus the gist in a few words. Group related messages into one row with a count badge. Skip bots, joins, and anything already answered by the owner.
- mail: ONE card, src "gmail" (or "outlook" when only Outlook is connected), across every connected email account, Gmail and Outlook alike. The eyebrow already names the app, so the title is the unread count, "<N> unread" (e.g. "5 unread", "1 unread"), where N counts the unread threads from real people across every account (not newsletters or notifications); if nothing is unread but rows remain, "<N> waiting" with N = the rows. short = "<N> unread emails". Rows are threads where a real person is waiting on the owner: label = sender, detail = what they need. Ignore newsletters, receipts and notifications. With more than one account, use the row tier for the account (tier 0 = the first account, 1 = the second, 2 = the third) and set "sections" to short account names in that order (e.g. ["WORK", "PERSONAL", "SIDE"] from the address or domain, never a full address). With one account, use tiers for urgency as usual.
- teams: ONE card, src "teams", from the Microsoft 365 connector's Teams chat search, built like the Slack card, title included ("<N> unread", short "<N> unread in Teams"): rows are people or channels plus the gist, tiers 0 = needs the owner now, 1 = today, 2 = when they can.
- support: ONE card titled "Customer inbox", src "intercom": open Intercom conversations waiting on the owner or their team. Rows: label = customer or company (max 22), detail = what they need, badge = how long they have waited ("3h", "2d"). Tiers: 0 = waiting on the owner or past the team's reply target, 1 = assigned to the owner, 2 = unassigned in the team inbox. subtitle like "5 waiting, oldest 2d". Nothing if the inbox is clear.
- task: issues assigned to the owner that are due within 2 days or that someone is blocked on. One card per issue (max 3) or one card with rows.
- ship: only failures that are still failing (latest production deploy errored, error spikes in the last hours). urgent = true. Nothing if all is green.
- money: one card, big = today's gross so far, delta = against the same weekday's usual, spark = cumulative revenue through today (up to 24 points, 0-100 scale). Only if the data is readily available.
- music: (ONCE is the owner's music release platform) one card for the most recent release, src "once", subtitle = the release name, big = streams this week (e.g. "12.4k"), delta vs last week, spark = daily trend.
- quest: (Tavern is where the owner's tabletop group keeps its campaign) src "tavern": the next scheduled tabletop session (title = campaign or adventure name, at = start) and in body where the owner's character stands (HP, notable state, last cliffhanger). Only if a session is within 7 days.
- focus: if there is a free block of 45+ minutes later today, one card for it: start = block start, end = block end, title like "Free until 4:30. Finish the rate limiter review." with the single highest-leverage item from everything above.
- brief: before 11 AM one morning brief card (start now, end 11 AM): the day in one line in title, body with the 2-3 things that matter. After 4 PM an evening wrap card (what got done, what tomorrow starts with).
- doc: docs shared with the owner or with unresolved comments for them in the last 2 days, from every Google Drive account and SharePoint/OneDrive, plus the doc to read before the next meeting when there is one. src "gdrive" or "m365".
- other: anything genuinely important that fits nowhere else.
- traffic: one card from Vercel Web Analytics for the owner's main site: big = visitors today so far (e.g. "8,312"), delta against the same weekday's usual, spark = hourly visitors today, subtitle = top page and the biggest referrer. Only when it is unusual (well above or below normal) or the owner launched something; priority 20-45. C-Ink builds a separate Traffic card from Cloudflare itself (listed below when there is one); never make one for a Cloudflare site.
- db: one card from Supabase advisors and logs: rows for security warnings (RLS off, exposed functions: tier 0, urgent if a table holding user data is public), performance warnings (tier 2), paused or unhealthy projects (tier 1). label = table or function, badge like "RLS" or "IDX". Nothing if clean.
- commute: for each in-person meeting in the next 12 hours that has a physical location (an address or a named place in the location field or invite, not a video link or a room in the owner's own office), one card. Work out where the owner will be right before it: the location of their previous in-person meeting that day if there is one, otherwise their usual starting point given below. Skip it if both are the same building. Estimate door-to-door time for the owner's usual way of getting around from what you know about the two places and the time of day (rush hour, a bridge, parking), round up to 5 minutes and add the owner's buffer. "at" = leave-by time (meeting start minus that estimate), "start" = 45 minutes before leave-by, "end" = the meeting start. title = "<mode verb> to <place>", e.g. "Drive to Blue Bottle Hayes Valley" (max 40). subtitle like "About 25 min by car" (say "about": it is an estimate without live traffic). body = the meeting it is for and its start time, plus one practical note if there is a real one (parking, which entrance, the train line). meta = the destination's street address, shortened. Priority 45-70. Never route to or from anything covered by the privacy rule.
- wins: Thursday afternoon onward, or any time the day is quiet, one card for the week so far: rows for merged PRs (count badge), Linear issues completed, deploys or releases that went live (ONCE, Vercel). Title with some warmth ("A good week so far"). Priority 25-40, higher on Friday afternoon. Never pad it: only real, completed work.
- parcel: ONE card titled "Deliveries" for packages arriving today or tomorrow, or delayed. Use the Shop connector's orders and tracking when it is connected, and order and shipping emails in the mail connectors (Shop, Amazon, carriers). Rows: label = merchant (max 22), detail = item in a few words and status ("Arriving today by 8 PM", "Delayed, now Thursday"), badge "!" for a delay or a signature needed. Tiers: 0 = arriving today or needs action, 1 = tomorrow, 2 = in transit. sections ["TODAY", "TOMORROW", "ON THE WAY"]. src "shop" when any row came from Shop, else the mail connector the emails came from ("gmail" or "outlook"). Priority 25-45, higher on the day something arrives. Nothing if no package is moving.
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

/// Calendar tools only, for the quick pass.
pub fn calendar_tools(tools: &[String]) -> Vec<String> {
    tools
        .iter()
        .filter(|t| {
            let l = t.to_lowercase();
            l.contains("calendar") && !l.contains("free_busy")
        })
        .cloned()
        .collect()
}

pub const QUICK_SCHEMA: &str = r#"{
  "type": "object",
  "properties": {
    "events": {
      "type": "array",
      "maxItems": 20,
      "items": {
        "type": "object",
        "properties": {
          "id": {"type": "string", "description": "cal: + the event id exactly as the connector gives it"},
          "title": {"type": "string"},
          "at": {"type": "string", "description": "ISO 8601 start with offset"},
          "end": {"type": "string", "description": "ISO 8601 end with offset"},
          "who": {"type": "string", "description": "Up to 3 attendee first names, comma separated"},
          "where": {"type": "string", "description": "Room, address, or the video service (Meet, Zoom, Teams)"},
          "src": {"type": "string", "enum": ["gcal", "outlook"]}
        },
        "required": ["id", "title", "at"]
      }
    }
  },
  "required": ["events"]
}"#;

/// The quick pass between smart runs: plain calendar facts, no judgement.
pub fn quick(now: DateTime<Local>, private_terms: &[String]) -> String {
    let mut p = format!(
        "It is {}. Using only the calendar tools, list every event on every connected calendar from now until 12 hours \
         from now, plus the first event tomorrow. Skip declined and cancelled events and all-day placeholders. Copy the \
         facts as they are: no summaries, no advice. Do not read anything except the calendars.",
        now.format("%A %B %-d, %Y, %-I:%M %p (UTC%:z)")
    );
    if !private_terms.is_empty() {
        p.push_str(&format!(" Leave out any event involving: {}.", private_terms.join(", ")));
    }
    p
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
        assert!(p.contains("\"<N> unread in Slack\"") && !p.contains("Slack, sorted"));
    }

    #[test]
    fn privacy_terms_reach_prompt() {
        let mut s = Settings::default();
        s.private_terms = vec!["Project X".into()];
        let p = build(&s, Local::now(), &[], None, &[]);
        assert!(p.contains("PRIVACY RULE") && p.contains("Project X"));
    }
}
