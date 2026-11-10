#include "cink/model.h"

#include <string.h>

#include "cink/json.h"

namespace cink {

static const char* const kKeys[] = {"event", "review", "chat",  "mail",  "task",    "ship", "money",
                                    "music", "quest",  "focus", "brief", "doc",     "other",
                                    "traffic", "db",   "commute", "wins"};
static const char* const kLabels[] = {"CALENDAR", "REVIEW", "SLACK", "INBOX", "LINEAR",  "SHIP",  "STRIPE",
                                      "MUSIC",    "QUEST",  "FOCUS", "BRIEF", "DOCS",    "NOTE",
                                      "TRAFFIC",  "DATABASE", "COMMUTE", "THIS WEEK"};
static_assert(sizeof kKeys / sizeof kKeys[0] == (size_t)Kind::Count, "kind keys");
static_assert(sizeof kLabels / sizeof kLabels[0] == (size_t)Kind::Count, "kind labels");

const char* kind_key(Kind k) { return (int)k < (int)Kind::Count ? kKeys[(int)k] : "other"; }
const char* kind_label(Kind k) { return (int)k < (int)Kind::Count ? kLabels[(int)k] : "NOTE"; }

Kind kind_from_key(const char* s) {
  for (int i = 0; i < (int)Kind::Count; i++)
    if (strcmp(s, kKeys[i]) == 0) return (Kind)i;
  return Kind::Other;
}

static int clamp(int64_t v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : (int)v; }

static void parse_row(Json& j, Row& r) {
  memset(&r, 0, sizeof r);
  r.tier = 1;
  char key[8];
  if (!j.obj_begin()) return;
  while (j.obj_next(key, sizeof key)) {
    int64_t n;
    if (!strcmp(key, "l")) j.str(r.label, sizeof r.label);
    else if (!strcmp(key, "d")) j.str(r.detail, sizeof r.detail);
    else if (!strcmp(key, "g")) j.str(r.badge, sizeof r.badge);
    else if (!strcmp(key, "tr")) { if (j.i64(n)) r.tier = (uint8_t)clamp(n, 0, 2); }
    else if (!strcmp(key, "u")) j.boolean(r.urgent);
    else j.skip();
  }
}

static void parse_card(Json& j, Card& c) {
  memset(&c, 0, sizeof c);
  c.kind = Kind::Other;
  c.priority = 50;
  char key[8];
  if (!j.obj_begin()) return;
  while (j.obj_next(key, sizeof key)) {
    int64_t n;
    if (!strcmp(key, "id")) j.str(c.id, sizeof c.id);
    else if (!strcmp(key, "k")) { char k[12]; if (j.str(k, sizeof k)) c.kind = kind_from_key(k); }
    else if (!strcmp(key, "t")) j.str(c.title, sizeof c.title);
    else if (!strcmp(key, "sh")) j.str(c.shrt, sizeof c.shrt);
    else if (!strcmp(key, "s")) j.str(c.subtitle, sizeof c.subtitle);
    else if (!strcmp(key, "b")) j.str(c.body, sizeof c.body);
    else if (!strcmp(key, "m")) j.str(c.meta, sizeof c.meta);
    else if (!strcmp(key, "big")) j.str(c.big, sizeof c.big);
    else if (!strcmp(key, "dl")) j.str(c.delta, sizeof c.delta);
    else if (!strcmp(key, "p")) { if (j.i64(n)) c.priority = (int16_t)clamp(n, 0, 100); }
    else if (!strcmp(key, "u")) j.boolean(c.urgent);
    else if (!strcmp(key, "st")) { if (j.null()) c.start = 0; else j.i64(c.start); }
    else if (!strcmp(key, "en")) { if (j.null()) c.end = 0; else j.i64(c.end); }
    else if (!strcmp(key, "at")) { if (j.null()) c.at = 0; else j.i64(c.at); }
    else if (!strcmp(key, "sp")) {
      if (j.arr_begin())
        while (j.arr_next()) {
          if (j.i64(n) && c.nspark < Card::MAX_SPARK) c.spark[c.nspark++] = (uint8_t)clamp(n, 0, 100);
        }
    } else if (!strcmp(key, "r")) {
      if (j.arr_begin())
        while (j.arr_next()) {
          if (c.nrows < Card::MAX_ROWS) parse_row(j, c.rows[c.nrows++]);
          else j.skip();
        }
    } else j.skip();
  }
}

static void parse_cfg(Json& j, DeviceSettings& c) {
  c.present = true;
  c.quiet_start = c.quiet_end = -1;
  char key[8];
  if (!j.obj_begin()) return;
  while (j.obj_next(key, sizeof key)) {
    int64_t n;
    if (!strcmp(key, "rf")) { if (j.i64(n)) c.refresh_min = clamp(n, 5, 240); }
    else if (!strcmp(key, "dis")) { if (j.i64(n)) c.disabled = (uint32_t)n; }
    else if (!strcmp(key, "mono")) j.boolean(c.mono);
    else if (!strcmp(key, "rot")) { if (j.i64(n)) c.rotation = n ? 1 : 0; }
    else if (!strcmp(key, "tz")) j.str(c.tz, sizeof c.tz);
    else if (!strcmp(key, "q")) {
      int i = 0;
      if (j.arr_begin())
        while (j.arr_next()) {
          if (j.i64(n)) {
            if (i == 0) c.quiet_start = (int8_t)clamp(n, -1, 23);
            else if (i == 1) c.quiet_end = (int8_t)clamp(n, -1, 23);
          }
          i++;
        }
    } else j.skip();
  }
}

bool parse_digest(const char* json, size_t len, Digest& d, const char** err) {
  memset(&d, 0, sizeof d);
  Json j(json, len);
  char key[12];
  bool saw_cards = false;
  if (!j.obj_begin()) { if (err) *err = "not an object"; return false; }
  while (j.obj_next(key, sizeof key)) {
    int64_t n;
    if (!strcmp(key, "v")) { if (j.i64(n)) d.version = (int)n; }
    else if (!strcmp(key, "gen")) j.i64(d.generated);
    else if (!strcmp(key, "hz")) j.i64(d.horizon);
    else if (!strcmp(key, "hl")) j.str(d.headline, sizeof d.headline);
    else if (!strcmp(key, "cfg")) parse_cfg(j, d.cfg);
    else if (!strcmp(key, "cards")) {
      saw_cards = true;
      if (j.arr_begin())
        while (j.arr_next()) {
          if (d.ncards < Digest::MAX_CARDS) {
            Card& c = d.cards[d.ncards];
            parse_card(j, c);
            if (c.title[0]) d.ncards++;  // a card with no title is not worth a screen
          } else {
            j.skip();
          }
        }
    } else j.skip();
  }
  if (!j.ok()) { if (err) *err = "malformed json"; return false; }
  if (d.version != 1) { if (err) *err = "unsupported version"; return false; }
  if (!saw_cards || d.generated <= 0) { if (err) *err = "missing fields"; return false; }
  if (d.horizon <= d.generated) d.horizon = d.generated + 12 * 3600;
  return true;
}

}  // namespace cink
