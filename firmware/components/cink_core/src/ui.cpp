#include "cink/ui.h"

#include <stdio.h>
#include <string.h>

#include "cink/icons.h"
#include "cink/timefmt.h"

namespace cink {

namespace {

constexpr int W = Canvas::W, H = Canvas::H;
constexpr int M = 28;            // outer margin
constexpr int BAR_Y = 30;        // status bar baseline
constexpr int BAR_RULE = 46;
constexpr int HERO_Y = 62;
constexpr int FOOT_RULE = 366;
constexpr int FOOT_Y = 378;
constexpr int TRACK_CAPS = 6;    // 1.5 px letterspacing for small caps

Ink accent_ink(const Status& st) { return st.mono ? Ink::Black : Ink::Accent; }

void upper(const char* in, char* out, size_t n) {
  size_t i = 0;
  for (; in[i] && i + 1 < n; i++) {
    char ch = in[i];
    out[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
  }
  out[i] = 0;
}

int eyebrow(Canvas& cv, int x, int y, const char* s, Ink ink, int max_w = 0) {
  char buf[96];
  upper(s, buf, sizeof buf);
  return cv.text(font_caption, x, y, buf, ink, TRACK_CAPS, max_w);
}

// ---------------------------------------------------------------- status bar

void status_bar(Canvas& cv, const Status& st) {
  char day[32];
  fmt_day(st.now, day, sizeof day);
  eyebrow(cv, M, BAR_Y, day, Ink::Black);

  int x = W - M - 26;
  if (st.battery >= 0) {
    icon_battery(cv, x, BAR_Y - 11, st.battery, st.charging, st.battery <= 15 ? accent_ink(st) : Ink::Black);
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", st.battery);
    int w = Canvas::measure(font_caption, pct, TRACK_CAPS);
    x -= w + 8;
    cv.text(font_caption, x, BAR_Y, pct, Ink::Black, TRACK_CAPS);
  }
  x -= 30;
  icon_wifi(cv, x, BAR_Y - 13, st.net == Net::Ok, Ink::Black);

  char sync[48];
  if (st.net == Net::NeverSynced || !st.last_sync) {
    snprintf(sync, sizeof sync, "NOT SYNCED YET");
  } else if (st.net == Net::Offline) {
    char ago[24];
    fmt_ago(st.now, st.last_sync, ago, sizeof ago);
    snprintf(sync, sizeof sync, "OFFLINE  \xC2\xB7  %s", ago);
  } else {
    char clock[16];
    fmt_clock(st.last_sync, clock, sizeof clock);
    snprintf(sync, sizeof sync, "SYNCED %s", clock);
  }
  char up[48];
  upper(sync, up, sizeof up);
  int w = Canvas::measure(font_caption, up, TRACK_CAPS);
  cv.text(font_caption, x - 12 - w, BAR_Y, up, Ink::Black, TRACK_CAPS);
  cv.hline(M, BAR_RULE, W - 2 * M, Ink::Black, 2);
}

// ------------------------------------------------------------- hero layouts

struct Box { int x, y, w, h; };

void urgent_bar(Canvas& cv, const Status& st, int y, int h) { cv.fill(0, y, 8, h, accent_ink(st)); }

// Eyebrow for a card: icon + KIND · context
void card_eyebrow(Canvas& cv, const Card& c, const Status& st, int x, int y, int w, const char* ctx) {
  icon(cv, c.kind, x, y - 15, 18, c.urgent ? accent_ink(st) : Ink::Black);
  char buf[96];
  if (ctx && *ctx) snprintf(buf, sizeof buf, "%s  \xC2\xB7  %s", kind_label(c.kind), ctx);
  else snprintf(buf, sizeof buf, "%s", kind_label(c.kind));
  eyebrow(cv, x + 28, y, buf, Ink::Black, w - 28);
}

// Title in the largest size that keeps it to max_lines. Returns the baseline
// of the last line drawn.
int title_block(Canvas& cv, const char* s, int x, int y, int w, int max_lines) {
  const Font* f = &font_title;
  int lh = 52;
  int need = cv.wrap(*f, x, y, w, lh, 9, s, Ink::Black, true);
  if (need > max_lines) { f = &font_title_s; lh = 42; }
  int used = cv.wrap(*f, x, y, w, lh, max_lines, s, Ink::Black);
  return y + (used - 1) * lh;
}

void hero_standard(Canvas& cv, const Card& c, const Status& st, Box b, const char* ctx) {
  int y = b.y + 22;
  card_eyebrow(cv, c, st, b.x, y, b.w, ctx);
  y += 58;
  int last = title_block(cv, c.title, b.x, y, b.w, 2);
  y = last;
  int bottom = b.y + b.h - (c.meta[0] ? 34 : 6);
  if (c.subtitle[0]) {
    y += 40;
    cv.text(font_sub, b.x, y, c.subtitle, Ink::Black, 0, b.w);
  }
  if (c.body[0]) {
    // Short bodies get the larger face when the space is there.
    int room_sub = (bottom - (y + 40)) / 34 + 1;
    int need_sub = cv.wrap(font_sub, b.x, 0, b.w, 34, 9, c.body, Ink::Black, true);
    if (need_sub <= room_sub && need_sub <= 3) {
      y += 40;
      cv.wrap(font_sub, b.x, y, b.w, 34, room_sub, c.body, Ink::Black);
    } else {
      y += 36;
      int lines = (bottom - y) / 30 + 1;
      if (lines > 0) cv.wrap(font_body, b.x, y, b.w, 30, lines, c.body, Ink::Black);
    }
  }
  if (c.meta[0]) cv.text(font_small, b.x, b.y + b.h - 8, c.meta, Ink::Black, 0, b.w);
}

void hero_countdown(Canvas& cv, const Card& c, const Status& st, Box b) {
  int64_t dt = c.at - st.now;
  int mins = (int)((dt + 59) / 60);
  const int col = 236;
  int cx = b.x;
  char clock[16];
  fmt_clock(c.at, clock, sizeof clock);

  if (mins <= 1) {
    int w = Canvas::measure(font_big, "Now");
    cv.text(font_big, cx + (col - 20 - w) / 2, b.y + 150, "Now", c.urgent ? accent_ink(st) : Ink::Black);
  } else {
    char num[8];
    snprintf(num, sizeof num, "%d", mins);
    int w = Canvas::measure(font_huge, num);
    cv.text(font_huge, cx + (col - 20 - w) / 2, b.y + 168, num, c.urgent ? accent_ink(st) : Ink::Black);
    const char* unit = mins == 1 ? "MINUTE" : "MINUTES";
    int uw = Canvas::measure(font_caption, unit, TRACK_CAPS);
    cv.text(font_caption, cx + (col - 20 - uw) / 2, b.y + 204, unit, Ink::Black, TRACK_CAPS);
  }
  cv.vline(cx + col, b.y + 12, b.h - 24, Ink::Black, 2);

  Box r = {cx + col + 26, b.y, b.w - col - 26, b.h};
  char ctx[40];
  snprintf(ctx, sizeof ctx, "AT %s", clock);
  int y = r.y + 22;
  card_eyebrow(cv, c, st, r.x, y, r.w, ctx);
  y += 50;
  int used = cv.wrap(font_title_s, r.x, y, r.w, 42, 3, c.title, Ink::Black);
  y += (used - 1) * 42;
  int bottom = r.y + r.h - (c.meta[0] ? 34 : 6);
  if (c.subtitle[0] && y + 36 <= bottom) {
    y += 36;
    cv.text(font_body_b, r.x, y, c.subtitle, Ink::Black, 0, r.w);
  }
  if (c.body[0] && y + 32 <= bottom) {
    y += 32;
    int lines = (bottom - y) / 28 + 1;
    cv.wrap(font_body, r.x, y, r.w, 28, lines, c.body, Ink::Black);
  }
  if (c.meta[0]) cv.text(font_small, r.x, r.y + r.h - 8, c.meta, Ink::Black, 0, r.w);
}

const char* tier_name(int t) {
  switch (t) {
    case 0: return "NEEDS YOU NOW";
    case 1: return "TODAY";
    default: return "WHEN YOU CAN";
  }
}

void hero_rows(Canvas& cv, const Card& c, const Status& st, Box b, const char* ctx) {
  int y = b.y + 22;
  card_eyebrow(cv, c, st, b.x, y, b.w, ctx);
  y += 46;
  int more_w = c.nrows > 3 ? 110 : 0;  // room for a "+N MORE" tag
  cv.text(font_title_s, b.x, y, c.title, Ink::Black, 0, b.w - more_w);
  y += 12;
  bool tiers = false;
  for (int i = 1; i < c.nrows; i++) if (c.rows[i].tier != c.rows[0].tier) tiers = true;
  int limit = b.y + b.h;
  // How many rows fit? Overflow is announced next to the title instead of
  // spending a line on it.
  int fit = 0;
  {
    int yy = y, lt = -1;
    for (int i = 0; i < c.nrows; i++) {
      int need = 36 + ((tiers && c.rows[i].tier != lt) ? 30 : 0);
      if (yy + need > limit) break;
      yy += need;
      lt = c.rows[i].tier;
      fit++;
    }
  }
  if (fit < c.nrows) {
    char more[24];
    snprintf(more, sizeof more, "+%d MORE", c.nrows - fit);
    int mw = Canvas::measure(font_caption, more, TRACK_CAPS);
    cv.text(font_caption, b.x + b.w - mw, y - 12, more, Ink::Black, TRACK_CAPS);
  }
  int last_tier = -1;
  for (int i = 0; i < fit; i++) {
    const Row& r = c.rows[i];
    if (tiers && r.tier != last_tier) {
      y += 30;
      int tw = eyebrow(cv, b.x, y, tier_name(r.tier), r.tier == 0 ? accent_ink(st) : Ink::Black);
      cv.dots(b.x + tw + 12, y - 5, b.w - tw - 12, 3, Ink::Black);
      last_tier = r.tier;
    }
    y += 36;
    int x = b.x;
    if (r.badge[0]) {
      int bw = Canvas::measure(font_caption, r.badge, 2) + 16;
      if (bw < 26) bw = 26;
      Ink fill = r.urgent ? accent_ink(st) : Ink::Black;
      cv.round_fill(x, y - 19, bw, 24, 12, fill);
      int tw = Canvas::measure(font_caption, r.badge, 2);
      cv.text(font_caption, x + (bw - tw) / 2, y - 2, r.badge, Ink::White, 2);
      x += bw + 12;
    } else {
      cv.circle_fill(x + 5, y - 7, 4, r.urgent ? accent_ink(st) : Ink::Black);
      x += 20;
    }
    int lw = cv.text(font_body_b, x, y, r.label, Ink::Black, 0, (b.x + b.w - x) / 2);
    x += lw + 12;
    if (r.detail[0] && x < b.x + b.w - 40) cv.text(font_body, x, y, r.detail, Ink::Black, 0, b.x + b.w - x);
  }
}

void sparkline(Canvas& cv, const Card& c, const Status& st, Box b) {
  if (c.nspark < 2) return;
  int lo = 100, hi = 0;
  for (int i = 0; i < c.nspark; i++) {
    if (c.spark[i] < lo) lo = c.spark[i];
    if (c.spark[i] > hi) hi = c.spark[i];
  }
  if (hi == lo) { hi = lo + 1; }
  cv.dots(b.x, b.y + b.h, b.w, 4, Ink::Black);
  int px = 0, py = 0;
  for (int i = 0; i < c.nspark; i++) {
    int x = b.x + i * (b.w - 1) / (c.nspark - 1);
    int y = b.y + b.h - (c.spark[i] - lo) * b.h / (hi - lo);
    if (i) cv.line(px, py, x, y, Ink::Black, 3);
    px = x;
    py = y;
  }
  cv.circle_fill(px, py, 7, c.urgent ? accent_ink(st) : Ink::Black);
  cv.circle_fill(px, py, 3, Ink::White);
}

void hero_metric(Canvas& cv, const Card& c, const Status& st, Box b, const char* ctx) {
  int y = b.y + 22;
  card_eyebrow(cv, c, st, b.x, y, b.w, ctx);
  int left_w = c.nspark >= 2 ? b.w - 300 : b.w;
  y += 92;
  cv.text(font_big, b.x, y, c.big, Ink::Black, 0, left_w);
  if (c.delta[0]) cv.text(font_body_b, b.x, y + 38, c.delta, c.urgent ? accent_ink(st) : Ink::Black, 0, left_w);
  sparkline(cv, c, st, {b.x + b.w - 270, b.y + 48, 270, 104});
  y += 96;
  cv.text(font_title_s, b.x, y, c.title, Ink::Black, 0, b.w);
  int bottom = b.y + b.h - (c.meta[0] ? 34 : 6);
  if (c.subtitle[0] && y + 36 <= bottom) {
    y += 36;
    cv.text(font_sub, b.x, y, c.subtitle, Ink::Black, 0, b.w);
  }
  if (c.body[0] && y + 32 <= bottom) {
    y += 32;
    cv.wrap(font_body, b.x, y, b.w, 30, (bottom - y) / 30 + 1, c.body, Ink::Black);
  }
  if (c.meta[0]) cv.text(font_small, b.x, b.y + b.h - 8, c.meta, Ink::Black, 0, b.w);
}

void context_for(const Card& c, const Status& st, char* out, size_t n) {
  out[0] = 0;
  if (!c.at) return;
  int64_t dt = c.at - st.now;
  if (dt <= 0) {
    if (c.end > st.now) {
      char clock[16];
      fmt_clock(c.end, clock, sizeof clock);
      snprintf(out, n, "NOW UNTIL %s", clock);
    } else {
      snprintf(out, n, "NOW");
    }
    return;
  }
  char u[40];
  fmt_until(st.now, c.at, u, sizeof u);
  upper(u, out, n);
}

void hero(Canvas& cv, const Card& c, const Status& st, Box b) {
  if (c.urgent) urgent_bar(cv, st, b.y + 4, 92);
  char ctx[48];
  context_for(c, st, ctx, sizeof ctx);
  int64_t dt = c.at ? c.at - st.now : -1;
  if (c.at && dt > 0 && dt <= 99 * 60 && (c.kind == Kind::Event || c.kind == Kind::Quest)) {
    hero_countdown(cv, c, st, b);
  } else if (c.big[0]) {
    hero_metric(cv, c, st, b, ctx);
  } else if (c.nrows) {
    hero_rows(cv, c, st, b, ctx);
  } else {
    hero_standard(cv, c, st, b, ctx);
  }
}

// ------------------------------------------------------------------ footer

void tile(Canvas& cv, const Card& c, const Status& st, int x, int w) {
  int y = FOOT_Y;
  icon(cv, c.kind, x, y + 2, 16, c.urgent ? accent_ink(st) : Ink::Black);
  char label[40];
  if (c.at && c.at > st.now) fmt_when(st.now, c.at, label, sizeof label);
  else if (c.at) snprintf(label, sizeof label, "NOW");
  else snprintf(label, sizeof label, "%s", kind_label(c.kind));
  eyebrow(cv, x + 24, y + 16, label, Ink::Black, w - 24);
  cv.text(font_tile, x, y + 46, c.shrt[0] ? c.shrt : c.title, Ink::Black, 0, w);
  const char* sub = c.subtitle[0] ? c.subtitle : c.big[0] ? c.big : c.nrows ? c.rows[0].label : c.meta;
  if (sub && *sub) cv.text(font_small, x, y + 72, sub, Ink::Black, 0, w);
}

void footer(Canvas& cv, const Digest& d, const Ranked& r, int skip, const Status& st) {
  // The three cards after the one on screen, so paging always previews what is next.
  int ids[3], n = 0;
  for (int i = 1; i < r.n && n < 3; i++) ids[n++] = r.idx[(skip + i) % r.n];
  if (!n) return;
  cv.hline(M, FOOT_RULE, W - 2 * M, Ink::Black, 1);
  int tw = (W - 2 * M) / 3;
  for (int i = 0; i < n; i++) {
    int x = M + i * tw + (i ? 18 : 0);
    if (i) cv.vline(M + i * tw, FOOT_Y, 78, Ink::Black, 1);
    tile(cv, d.cards[ids[i]], st, x, tw - (i ? 36 : 18));
  }
}

void page_dots(Canvas& cv, int n, int cur) {
  if (n < 2) return;
  if (n > 12) n = 12;
  int gap = 14;
  int x0 = W / 2 - (n - 1) * gap / 2;
  for (int i = 0; i < n; i++) {
    if (i == cur) cv.circle_fill(x0 + i * gap, H - 10, 3, Ink::Black);
    else cv.circle(x0 + i * gap, H - 10, 3, Ink::Black, 1);
  }
}

// ----------------------------------------------------------- other screens

const Card* next_event(const Digest& d, int64_t now, uint32_t disabled) {
  const Card* best = nullptr;
  for (int i = 0; i < d.ncards; i++) {
    const Card& c = d.cards[i];
    if ((disabled & (1u << (int)c.kind)) || c.kind != Kind::Event || c.at <= now) continue;
    if (!best || c.at < best->at) best = &c;
  }
  return best;
}

void screen_empty(Canvas& cv, const Digest& d, const Status& st, uint32_t disabled) {
  cv.text(font_big, M, 190, "All clear.", Ink::Black);
  const char* line = d.headline[0] ? d.headline : "Nothing needs you right now.";
  if (st.net == Net::NeverSynced) line = "Waiting for the first sync from C-Ink Settings.";
  cv.wrap(font_sub, M, 244, W - 2 * M, 36, 2, line, Ink::Black);
  const Card* e = next_event(d, st.now, disabled);
  if (e) {
    cv.hline(M, FOOT_RULE, W - 2 * M, Ink::Black, 1);
    char when[40], buf[180];
    fmt_until(st.now, e->at, when, sizeof when);
    snprintf(buf, sizeof buf, "%s, %s", e->title, when);
    icon(cv, Kind::Event, M, FOOT_Y + 10, 22, Ink::Black);
    eyebrow(cv, M + 34, FOOT_Y + 28, "NEXT ON YOUR CALENDAR", Ink::Black);
    cv.text(font_tile, M, FOOT_Y + 64, buf, Ink::Black, 0, W - 2 * M);
  }
}

void screen_night(Canvas& cv, const Digest& d, const Status& st, uint32_t disabled) {
  icon_moon(cv, M, 92, 56, Ink::Black);
  cv.text(font_big, M, 232, "Good night.", Ink::Black);
  const Card* e = next_event(d, st.now, disabled);
  char buf[200];
  if (e) {
    char clock[16];
    fmt_clock(e->at, clock, sizeof clock);
    int days = day_diff(st.now, e->at);
    snprintf(buf, sizeof buf, "%s starts at %s with %s.", days == 0 ? "Today" : days == 1 ? "Tomorrow" : "Next up",
             clock, e->title);
  } else {
    snprintf(buf, sizeof buf, "Nothing on the calendar first thing.");
  }
  cv.wrap(font_sub, M, 290, W - 2 * M, 36, 2, buf, Ink::Black);
  if (st.quiet_end_hour >= 0) {
    char foot[64];
    int h = st.quiet_end_hour % 12;
    snprintf(foot, sizeof foot, "QUIET UNTIL %d:00 %s", h ? h : 12, st.quiet_end_hour < 12 ? "AM" : "PM");
    eyebrow(cv, M, H - 28, foot, Ink::Black);
  }
}

void step(Canvas& cv, int n, int y, const char* title, const char* body) {
  char num[4];
  snprintf(num, sizeof num, "%d", n);
  cv.circle(M + 18, y - 8, 18, Ink::Black, 2);
  int w = Canvas::measure(font_body_b, num);
  cv.text(font_body_b, M + 18 - w / 2, y, num, Ink::Black);
  cv.text(font_body_b, M + 54, y - 6, title, Ink::Black);
  cv.text(font_small, M + 54, y + 20, body, Ink::Black, 0, W - M - (M + 54));
}

void screen_setup(Canvas& cv, const Status& st) {
  cv.text(font_title, M, 96, "Let's set up C-Ink.", Ink::Black);
  step(cv, 1, 170, "Get C-Ink Settings", "The small Mac / Windows app from github.com/jakehandy/c-ink.");
  step(cv, 2, 246, "Plug in with USB-C", "The app finds this TICKEY and sends it your Wi-Fi.");
  step(cv, 3, 322, "Sign in with Claude", "Pick the screens you want. First sync takes about a minute.");
  cv.hline(M, 392, W - 2 * M, Ink::Black, 1);
  char buf[64];
  snprintf(buf, sizeof buf, "DEVICE  %s", st.device_id);
  eyebrow(cv, M, 428, buf, Ink::Black);
  snprintf(buf, sizeof buf, "C-INK %s", st.firmware);
  int w = Canvas::measure(font_caption, buf, TRACK_CAPS);
  eyebrow(cv, W - M - w, 428, buf, Ink::Black);
}

void kv(Canvas& cv, int y, const char* k, const char* v) {
  eyebrow(cv, M, y, k, Ink::Black);
  cv.text(font_body, M + 190, y + 2, v && *v ? v : "-", Ink::Black, 0, W - 2 * M - 190);
  cv.dots(M, y + 14, W - 2 * M, 3, Ink::Black);
}

void screen_info(Canvas& cv, const Digest& d, const Status& st) {
  cv.text(font_title_s, M, 100, "This TICKEY", Ink::Black);
  char buf[64], a[24];
  int y = 150;
  kv(cv, y, "DEVICE", st.device_id); y += 40;
  kv(cv, y, "FIRMWARE", st.firmware); y += 40;
  kv(cv, y, "WI-FI", st.ssid); y += 40;
  kv(cv, y, "ADDRESS", st.ip); y += 40;
  kv(cv, y, "SOURCE", st.source); y += 40;
  if (st.last_sync) { fmt_ago(st.now, st.last_sync, a, sizeof a); snprintf(buf, sizeof buf, "%s, %d cards", a, d.ncards); }
  else snprintf(buf, sizeof buf, "never");
  kv(cv, y, "LAST SYNC", buf); y += 40;
  if (st.next_sync) { fmt_clock(st.next_sync, a, sizeof a); kv(cv, y, "NEXT SYNC", a); }
  eyebrow(cv, M, H - 24, "HOLD BACK TO SYNC NOW   \xC2\xB7   UP / DOWN TO BROWSE", Ink::Black);
}

}  // namespace

int render(Canvas& cv, const Digest& d, const Status& st, const View& v, uint32_t disabled) {
  cv.clear();
  Ranked r;
  rank(d, st.now, disabled, r);

  switch (v.screen) {
    case Screen::Setup: screen_setup(cv, st); return r.n;
    case Screen::Night: screen_night(cv, d, st, disabled); return r.n;
    case Screen::Info: status_bar(cv, st); screen_info(cv, d, st); return r.n;
    default: break;
  }

  status_bar(cv, st);
  if (!r.n) {
    screen_empty(cv, d, st, disabled);
    return 0;
  }
  int cur = v.screen == Screen::Browse ? ((v.index % r.n) + r.n) % r.n : 0;
  bool has_footer = r.n > 1;
  Box b = {M, HERO_Y, W - 2 * M, (has_footer ? FOOT_RULE : H - 24) - HERO_Y - 10};
  hero(cv, d.cards[r.idx[cur]], st, b);
  if (has_footer) footer(cv, d, r, cur, st);
  page_dots(cv, r.n, cur);
  return r.n;
}

}  // namespace cink
