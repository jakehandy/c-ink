#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cink {

// Card kinds. Each one is a screen the user can switch off in Settings; the
// bit for kind K in a "disabled" mask is (1u << K).
enum class Kind : uint8_t {
  Event = 0,  // calendar: next meeting, countdown
  Review,     // GitHub: PRs that need you
  Chat,       // Slack: grouped by importance
  Mail,       // Gmail: threads waiting on a reply
  Task,       // Linear: issues due or blocking others
  Ship,       // Vercel / Cloudflare: failing deploys, prod errors
  Money,      // Stripe: today vs usual
  Music,      // ONCE: release performance
  Quest,      // Tavern: next tabletop session, your character
  Focus,      // a free block plus the one thing worth doing in it
  Brief,      // morning brief / evening wrap
  Doc,        // Drive: docs shared with you or waiting on your comments
  Other,
  Count
};

const char* kind_key(Kind k);    // wire name, e.g. "event"
const char* kind_label(Kind k);  // eyebrow label, e.g. "CALENDAR"
Kind kind_from_key(const char* s);

struct Row {
  char label[48];   // who / where: "#launch", "Priya", "PR 412"
  char detail[112]; // the gist
  char badge[8];    // short count or tag: "3", "!", "CI"
  uint8_t tier;     // 0 needs you now, 1 today, 2 when you can
  bool urgent;
};

struct Card {
  static constexpr int MAX_ROWS = 6;
  static constexpr int MAX_SPARK = 24;

  char id[40];
  Kind kind;
  char title[112];
  char shrt[40];      // short title for footer tiles
  char subtitle[112];
  char body[240];
  char meta[72];
  char big[20];       // headline number for Money/Music: "$1,240"
  char delta[40];     // "+18% vs a usual Tuesday"
  int16_t priority;   // 0..100, Claude's judgement
  bool urgent;        // deserves the accent color
  int64_t start;      // relevant from (unix s, 0 = always)
  int64_t end;        // relevant until (0 = until next sync)
  int64_t at;         // time anchor for countdowns (event start)
  Row rows[MAX_ROWS];
  uint8_t nrows;
  uint8_t spark[MAX_SPARK];  // 0..100
  uint8_t nspark;
};

// Settings from C-Ink Settings that ride along with every digest, so changes
// reach the device over Wi-Fi without plugging it in.
struct DeviceSettings {
  bool present;
  int refresh_min;     // 0 = not set
  uint32_t disabled;
  int8_t quiet_start;  // -1 = no quiet hours
  int8_t quiet_end;
  bool mono;
  char tz[64];
};

struct Digest {
  static constexpr int MAX_CARDS = 24;
  int version;
  int64_t generated;   // when the bridge built it
  int64_t horizon;     // the digest is trustworthy until here
  char headline[120];  // Claude's one-line read on the day
  Card cards[MAX_CARDS];
  uint8_t ncards;
  DeviceSettings cfg;
};

// Parses the wire JSON into `out`. Returns false (and leaves a reason in err)
// when the document is unusable. Unknown fields are ignored so older devices
// keep working when the bridge adds fields.
bool parse_digest(const char* json, size_t len, Digest& out, const char** err = nullptr);

}  // namespace cink
