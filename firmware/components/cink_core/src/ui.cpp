// The C-Ink UI, portrait 480 x 720.
//
//   status bar      day, sync state, Wi-Fi, battery
//   hero            the single most relevant card, in one of four layouts
//   up next         the next three cards, so paging always previews what comes,
//                   with a small Clawd standing on its top rule, posed for the
//                   card on screen
//
// Everything is black on white with one accent color, kept for what deserves
// it: urgency, the countdown, Clawd.
#include "cink/ui.h"

#include <stdio.h>
#include <string.h>

#include "cink/clawd.h"
#include "cink/icons.h"
#include "cink/timefmt.h"

namespace cink {

namespace {

constexpr int W = Canvas::W, H = Canvas::H;
constexpr int M = 26;            // outer margin
constexpr int BAR_Y = 34;        // status bar baseline
constexpr int BAR_RULE = 50;
constexpr int HERO_Y = 62;
constexpr int NEXT_ROW_H = 40;   // one "up next" row
constexpr int CLAWD_CELL = 6;    // the small Clawd on the up next rule: 72 x 48
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

int eyebrow_w(const char* s) {
  char buf[96];
  upper(s, buf, sizeof buf);
  return Canvas::measure(font_caption, buf, TRACK_CAPS);
}

void center(Canvas& cv, const Font& f, int y, const char* s, Ink ink) {
  int w = Canvas::measure(f, s);
  cv.text(f, (W - w) / 2, y, s, ink, 0, W - 2 * M);
}

// ---------------------------------------------------------------- status bar

void status_bar(Canvas& cv, const Status& st) {
  int x = W - M - 26;
  if (st.battery >= 0) {
    icon_battery(cv, x, BAR_Y - 11, st.battery, st.charging, st.battery <= 15 ? accent_ink(st) : Ink::Black);
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", st.battery);
    x -= Canvas::measure(font_caption, pct, TRACK_CAPS) + 8;
    cv.text(font_caption, x, BAR_Y, pct, Ink::Black, TRACK_CAPS);
  }
  x -= 30;
  icon_wifi(cv, x, BAR_Y - 13, st.net == Net::Ok, Ink::Black);

  char sync[48];
  if (st.net == Net::NeverSynced || !st.last_sync) {
    snprintf(sync, sizeof sync, "NOT SYNCED");
  } else if (st.net == Net::Offline) {
    char clock[16];
    fmt_clock(st.last_sync, clock, sizeof clock);
    snprintf(sync, sizeof sync, "OFFLINE \xC2\xB7 %s", clock);
  } else {
    char clock[16];
    fmt_clock(st.last_sync, clock, sizeof clock);
    snprintf(sync, sizeof sync, "SYNCED %s", clock);
  }
  int sw = eyebrow_w(sync);
  x -= 12 + sw;
  eyebrow(cv, x, BAR_Y, sync, st.net == Net::Offline ? accent_ink(st) : Ink::Black);

  char day[32];
  fmt_day(st.now, day, sizeof day);
  eyebrow(cv, M, BAR_Y, day, Ink::Black, x - M - 16);
  cv.hline(M, BAR_RULE, W - 2 * M, Ink::Black, 2);
}

// ------------------------------------------------------------- hero layouts

struct Box { int x, y, w, h; };

// Eyebrow for a card: icon + KIND · context
void card_eyebrow(Canvas& cv, const Card& c, const Status& st, int x, int y, int w, const char* ctx) {
  card_icon(cv, c, x, y - 15, 18, c.urgent ? accent_ink(st) : Ink::Black);
  char buf[96];
  if (ctx && *ctx) snprintf(buf, sizeof buf, "%s  \xC2\xB7  %s", kind_label(c.kind), ctx);
  else snprintf(buf, sizeof buf, "%s", kind_label(c.kind));
  eyebrow(cv, x + 28, y, buf, c.urgent ? accent_ink(st) : Ink::Black, w - 28);
}

// Title in the largest size that keeps it to max_lines. Returns the baseline
// of the last line drawn.
int title_block(Canvas& cv, const char* s, int x, int y, int w, int max_lines) {
  const Font* f = &font_title;
  int lh = 52;
  if (cv.wrap(*f, x, y, w, lh, 9, s, Ink::Black, true) > max_lines) { f = &font_title_s; lh = 42; max_lines++; }
  int used = cv.wrap(*f, x, y, w, lh, max_lines, s, Ink::Black);
  return y + (used - 1) * lh;
}

// Subtitle, body and meta under a title, sized to whatever room is left.
void details(Canvas& cv, const Card& c, Box b, int y) {
  int bottom = b.y + b.h - (c.meta[0] ? 44 : 4);
  if (c.subtitle[0] && y + 40 <= bottom) {
    y += 40;
    cv.text(font_sub, b.x, y, c.subtitle, Ink::Black, 0, b.w);
  }
  if (c.body[0] && y + 36 <= bottom) {
    // Short bodies get the larger face when the space is there.
    int room_sub = (bottom - (y + 42)) / 34 + 1;
    int need_sub = cv.wrap(font_sub, b.x, 0, b.w, 34, 9, c.body, Ink::Black, true);
    if (need_sub <= room_sub && need_sub <= 4) {
      cv.wrap(font_sub, b.x, y + 42, b.w, 34, room_sub, c.body, Ink::Black);
    } else {
      y += 36;
      int lines = (bottom - y) / 30 + 1;
      if (lines > 0) cv.wrap(font_body, b.x, y, b.w, 30, lines, c.body, Ink::Black);
    }
  }
  if (c.meta[0]) {
    cv.dots(b.x, b.y + b.h - 30, b.w, 3, Ink::Black);
    cv.text(font_small, b.x, b.y + b.h - 6, c.meta, Ink::Black, 0, b.w);
  }
}

void hero_standard(Canvas& cv, const Card& c, const Status& st, Box b, const char* ctx) {
  int y = b.y + 22;
  card_eyebrow(cv, c, st, b.x, y, b.w, ctx);
  y = title_block(cv, c.title, b.x, y + 62, b.w, 3);
  details(cv, c, b, y);
}

void hero_countdown(Canvas& cv, const Card& c, const Status& st, Box b) {
  int64_t dt = c.at - st.now;
  int mins = (int)((dt + 59) / 60);
  char clock[16], ctx[40];
  fmt_clock(c.at, clock, sizeof clock);
  snprintf(ctx, sizeof ctx, "AT %s", clock);
  int y = b.y + 22;
  card_eyebrow(cv, c, st, b.x, y, b.w, ctx);

  // The number is the screen: how long until it starts.
  Ink num_ink = (c.urgent || mins <= 5) ? accent_ink(st) : Ink::Black;
  int base = y + 140;
  int nw;
  if (mins <= 1) {
    nw = cv.text(font_big, b.x - 2, base - 20, "Now", num_ink);
  } else {
    char num[12];
    snprintf(num, sizeof num, "%d", mins);
    nw = cv.text(font_huge, b.x - 4, base, num, num_ink);
    const char* unit = mins == 1 ? "MINUTE" : "MINUTES";
    eyebrow(cv, b.x + nw + 14, base, unit, Ink::Black);
    eyebrow(cv, b.x + nw + 14, base - 26, "STARTS IN", Ink::Black);
  }
  y = base + 22;
  cv.hline(b.x, y, b.w, Ink::Black, 2);
  y += 42;
  int used = cv.wrap(font_title_s, b.x, y, b.w, 42, 3, c.title, Ink::Black);
  y += (used - 1) * 42;
  int bottom = b.y + b.h - (c.meta[0] ? 44 : 4);
  if (c.subtitle[0] && y + 36 <= bottom) {
    y += 36;
    cv.text(font_body_b, b.x, y, c.subtitle, Ink::Black, 0, b.w);
  }
  if (c.body[0] && y + 32 <= bottom) {
    y += 32;
    cv.wrap(font_body, b.x, y, b.w, 29, (bottom - y) / 29 + 1, c.body, Ink::Black);
  }
  if (c.meta[0]) {
    cv.dots(b.x, b.y + b.h - 30, b.w, 3, Ink::Black);
    cv.text(font_small, b.x, b.y + b.h - 6, c.meta, Ink::Black, 0, b.w);
  }
}

// How the owner is getting there, for the line beside the minutes the card's
// (eyebrow capitals): the card's delta ("by car"), else guessed from the title's verb ("Drive to ...").
void travel_mode(const Card& c, char* out, size_t n) {
  if (c.delta[0]) {
    snprintf(out, n, "%s", c.delta);
  } else {
    struct { const char* verb; const char* mode; } verbs[] = {
      {"Drive", "by car"}, {"Walk", "on foot"}, {"Bike", "by bike"}, {"Ride", "by bike"},
      {"Take", "by transit"}, {"Train", "by train"}, {"Bus", "by bus"}, {"Head", "door to door"},
    };
    snprintf(out, n, "door to door");
    for (auto& v : verbs)
      if (!strncmp(c.title, v.verb, strlen(v.verb))) { snprintf(out, n, "%s", v.mode); break; }
  }
}

// Commute: the trip's length is the number ("25 MINUTES BY CAR"), the place
// goes under it, and the eyebrow counts down to leaving.
void hero_commute(Canvas& cv, const Card& c, const Status& st, Box b) {
  int64_t dt = c.at ? c.at - st.now : 0;
  int leave_in = (int)((dt + 59) / 60);
  char leave[16], arrive[16], ctx[40];
  fmt_clock(c.at, leave, sizeof leave);
  fmt_clock(c.end, arrive, sizeof arrive);
  if (!c.at) ctx[0] = 0;
  else if (dt <= 60 && dt > -15 * 60) snprintf(ctx, sizeof ctx, "GO NOW");
  else if (dt > 0 && leave_in <= 90) snprintf(ctx, sizeof ctx, "LEAVE IN %d MIN", leave_in);
  else snprintf(ctx, sizeof ctx, "LEAVE BY %s", leave);
  int y = b.y + 22;
  card_eyebrow(cv, c, st, b.x, y, b.w, ctx);

  // Trip length: the digits in "big", else arrival minus leave-by.
  int trip = 0;
  for (const char* p = c.big; *p; p++) {
    if (*p >= '0' && *p <= '9') trip = trip * 10 + (*p - '0');
    else if (trip) break;
  }
  if (!trip && c.at && c.end > c.at) trip = (int)((c.end - c.at + 59) / 60);
  const bool soon = c.at && dt <= 5 * 60;
  Ink num_ink = (c.urgent || soon) ? accent_ink(st) : Ink::Black;
  int base = y + 140;
  if (trip > 0 && trip < 1000) {
    char num[8], mode[40];
    snprintf(num, sizeof num, "%d", trip);
    int nw = cv.text(font_huge, b.x - 4, base, num, num_ink);
    travel_mode(c, mode, sizeof mode);
    eyebrow(cv, b.x + nw + 14, base - 26, trip == 1 ? "MINUTE" : "MINUTES", Ink::Black);
    eyebrow(cv, b.x + nw + 14, base, mode, Ink::Black, b.w - nw - 14);
  } else {
    base = y + 60;
  }
  y = base + 22;
  cv.hline(b.x, y, b.w, Ink::Black, 2);
  y += 42;
  int used = cv.wrap(font_title_s, b.x, y, b.w, 42, 2, c.title, Ink::Black);
  y += (used - 1) * 42;
  int bottom = b.y + b.h - (c.meta[0] ? 44 : 4);
  if (c.at && y + 36 <= bottom) {
    char when[64];
    if (c.end) snprintf(when, sizeof when, "Leave %s  \xC2\xB7  arrive %s", leave, arrive);
    else snprintf(when, sizeof when, "Leave by %s", leave);
    y += 36;
    cv.text(font_body_b, b.x, y, when, soon ? accent_ink(st) : Ink::Black, 0, b.w);
  }
  if (c.subtitle[0] && y + 30 <= bottom) {
    y += 30;
    cv.text(font_body, b.x, y, c.subtitle, Ink::Black, 0, b.w);
  }
  if (c.body[0] && y + 36 <= bottom) {
    y += 36;
    cv.wrap(font_body, b.x, y, b.w, 29, (bottom - y) / 29 + 1, c.body, Ink::Black);
  }
  if (c.meta[0]) {
    cv.dots(b.x, b.y + b.h - 30, b.w, 3, Ink::Black);
    cv.text(font_small, b.x, b.y + b.h - 6, c.meta, Ink::Black, 0, b.w);
  }
}

// A dithered photo at 2x, accent pixels in black when the panel is mono.
void draw_image(Canvas& cv, const Image& im, int x, int y, const Status& st) {
  const int stride = im.w / 8;
  const Ink acc = accent_ink(st);
  for (int j = 0; j < im.h; j++)
    for (int i = 0; i < im.w; i++) {
      int bit = 0x80 >> (i & 7), at = j * stride + i / 8;
      if (im.accent[at] & bit) cv.fill(x + 2 * i, y + 2 * j, 2, 2, acc);
      else if (im.black[at] & bit) cv.fill(x + 2 * i, y + 2 * j, 2, 2, Ink::Black);
    }
}

// The big story: photo across the top, a large headline, one line of dek,
// and where it is from. Without a photo the headline takes the room.
void hero_story(Canvas& cv, const Card& c, const Image* im, const Status& st, Box b) {
  int y = b.y + 22;
  card_eyebrow(cv, c, st, b.x, y, b.w, c.meta);
  y += 18;
  if (im) {
    draw_image(cv, *im, b.x + (b.w - 2 * im->w) / 2, y, st);
    y += 2 * im->h + 56;
  } else {
    y += 52;
  }
  int bottom = b.y + b.h - 4;
  int lines = (bottom - y) / 52 + 1;
  if (lines > 3) lines = 3;
  y = title_block(cv, c.title, b.x, y, b.w, lines < 1 ? 1 : lines);
  if (c.subtitle[0] && y + 40 <= bottom)
    cv.wrap(font_body, b.x, y + 38, b.w, 30, (bottom - y - 38) / 30 + 1 > 2 ? 2 : (bottom - y - 38) / 30 + 1,
            c.subtitle, Ink::Black);
}

const char* tier_name(int t) {
  switch (t) {
    case 0: return "NEEDS YOU NOW";
    case 1: return "TODAY";
    default: return "WHEN YOU CAN";
  }
}

void badge(Canvas& cv, int x, int y, const char* s, Ink fill) {
  int tw = Canvas::measure(font_caption, s, 2);
  int bw = tw + 16 < 28 ? 28 : tw + 16;
  cv.round_fill(x, y - 19, bw, 25, 12, fill);
  cv.text(font_caption, x + (bw - tw) / 2, y - 1, s, Ink::White, 2);
}

// Lists (Slack, PRs, inbox, wins): each row is a label line and a detail line.
void hero_rows(Canvas& cv, const Card& c, const Status& st, Box b, const char* ctx, int narrow_below) {
  // Left edge of Clawd and anything drawn around him.
  const int clawd_left = W - M - clawd_w(CLAWD_CELL) - 6 * CLAWD_CELL - 12;
  int y = b.y + 22;
  card_eyebrow(cv, c, st, b.x, y, b.w, ctx);
  y += 50;
  int used = cv.wrap(font_title_s, b.x, y, b.w, 42, 2, c.title, Ink::Black);
  y += (used - 1) * 42 + 8;
  bool tiers = false;
  for (int i = 1; i < c.nrows; i++) if (c.rows[i].tier != c.rows[0].tier) tiers = true;
  const int row_h = 56, tier_h = 28;
  // How many rows fit; if not all, keep a line for "+N more".
  auto count_fit = [&](int limit) {
    int yy = y, lt = -1, n = 0;
    for (int i = 0; i < c.nrows; i++) {
      int need = row_h + ((tiers && c.rows[i].tier != lt) ? tier_h : 0);
      if (yy + need > limit) break;
      yy += need;
      lt = c.rows[i].tier;
      n++;
    }
    return n;
  };
  int fit = count_fit(b.y + b.h);
  if (fit < c.nrows) fit = count_fit(b.y + b.h - 28);

  int last_tier = -1;
  for (int i = 0; i < fit; i++) {
    const Row& r = c.rows[i];
    if (tiers && r.tier != last_tier) {
      y += tier_h;
      // Cards can name their own sections (inbox accounts, news desks).
      const char* name = c.sec[r.tier][0] ? c.sec[r.tier] : tier_name(r.tier);
      Ink ink = (r.tier == 0 && !c.sec[0][0]) ? accent_ink(st) : Ink::Black;
      char up[24];
      upper(name, up, sizeof up);
      int tw = eyebrow(cv, b.x, y, up, ink);
      cv.dots(b.x + tw + 12, y - 5, (y - 12 > narrow_below ? clawd_left : b.x + b.w) - b.x - tw - 12, 3, Ink::Black);
      last_tier = r.tier;
    }
    y += 32;
    int x = b.x;
    Ink mark = r.urgent ? accent_ink(st) : Ink::Black;
    if (r.badge[0]) {
      badge(cv, x, y, r.badge, mark);
    } else {
      cv.circle_fill(x + 7, y - 7, 5, mark);
    }
    x += 52;
    int right = b.x + b.w;
    cv.text(font_body_b, x, y, r.label, Ink::Black, 0, (y > narrow_below ? clawd_left : right) - x);
    if (r.detail[0]) cv.text(font_small, x, y + 26, r.detail, Ink::Black, 0, (y + 26 - 20 > narrow_below ? clawd_left : right) - x);
    y += row_h - 32;
  }
  if (fit < c.nrows) {
    char more[48];
    snprintf(more, sizeof more, "+%d MORE", c.nrows - fit);
    eyebrow(cv, b.x + 52, y + 22, more, Ink::Black);
  }
}

void sparkline(Canvas& cv, const Card& c, const Status& st, Box b) {
  if (c.nspark < 2) return;
  int lo = 100, hi = 0;
  for (int i = 0; i < c.nspark; i++) {
    if (c.spark[i] < lo) lo = c.spark[i];
    if (c.spark[i] > hi) hi = c.spark[i];
  }
  if (hi == lo) hi = lo + 1;
  cv.dots(b.x, b.y + b.h, b.w, 4, Ink::Black);
  // A light fill under the line reads as an area chart on e-paper.
  int px = 0, py = 0;
  for (int i = 0; i < c.nspark; i++) {
    int x = b.x + i * (b.w - 1) / (c.nspark - 1);
    int y = b.y + b.h - (c.spark[i] - lo) * b.h / (hi - lo);
    if (i) {
      for (int xx = px; xx <= x; xx++) {
        int yy = py + (y - py) * (xx - px) / (x - px ? x - px : 1);
        cv.tint(xx, yy + 4, 1, b.y + b.h - yy - 4, 1);
      }
      cv.line(px, py, x, y, Ink::Black, 3);
    }
    px = x;
    py = y;
  }
  cv.circle_fill(px, py, 8, c.urgent ? accent_ink(st) : Ink::Black);
  cv.circle_fill(px, py, 3, Ink::White);
}

void hero_metric(Canvas& cv, const Card& c, const Status& st, Box b, const char* ctx) {
  int y = b.y + 22;
  card_eyebrow(cv, c, st, b.x, y, b.w, ctx);
  y += 90;
  int icon_w = 0;
  if (c.kind == Kind::Weather && c.wx != Wx::None) {
    icon_w = 104;
    wx_icon(cv, c.wx, b.x + b.w - 92, y - 72, 92, Ink::Black);
  }
  cv.text(font_big, b.x - 2, y, c.big, Ink::Black, 0, b.w - icon_w);
  if (c.delta[0]) cv.text(font_body_b, b.x, y + 38, c.delta, c.urgent ? accent_ink(st) : Ink::Black, 0, b.w - icon_w);
  y += 52;
  if (c.nspark >= 2) {
    sparkline(cv, c, st, {b.x, y + 14, b.w, 88});
    y += 112;
  }
  y += 42;
  int used = cv.wrap(font_title_s, b.x, y, b.w, 42, 2, c.title, Ink::Black);
  details(cv, c, b, y + (used - 1) * 42);
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

bool is_countdown(const Card& c, const Status& st) {
  int64_t dt = c.at ? c.at - st.now : -1;
  return c.at && dt > 0 && dt <= 99 * 60 && (c.kind == Kind::Event || c.kind == Kind::Quest);
}

void hero(Canvas& cv, const Digest& d, const Card& c, const Status& st, Box b) {
  if (c.urgent) cv.fill(0, b.y + 4, 8, 96, accent_ink(st));
  char ctx[48];
  context_for(c, st, ctx, sizeof ctx);
  if (c.kind == Kind::Story) hero_story(cv, c, image_for(d, c), st, b);
  else if (c.kind == Kind::Commute) hero_commute(cv, c, st, b);
  else if (is_countdown(c, st)) hero_countdown(cv, c, st, b);
  else if (c.big[0]) hero_metric(cv, c, st, b, ctx);
  else if (c.nrows) {
    // Lists may run down beside Clawd's head: rows that low are narrowed instead.
    Box tall = b;
    tall.h += 4 * CLAWD_CELL;
    hero_rows(cv, c, st, tall, ctx, b.y + b.h);
  }
  else hero_standard(cv, c, st, b, ctx);
}

// ----------------------------------------------------------------- up next

// One line per card: icon, short title, and when (or its number) on the right.
void next_row(Canvas& cv, const Card& c, const Status& st, int top) {
  Ink mark = c.urgent ? accent_ink(st) : Ink::Black;
  card_icon(cv, c, M, top + 9, 18, mark);
  char when[40];
  if (c.at && c.at > st.now) fmt_when(st.now, c.at, when, sizeof when);
  else if (c.at) snprintf(when, sizeof when, "NOW");
  else if (c.big[0]) snprintf(when, sizeof when, "%s", c.big);
  else if (c.urgent) snprintf(when, sizeof when, "URGENT");
  else snprintf(when, sizeof when, "%s", kind_label(c.kind));
  int ww = eyebrow_w(when);
  eyebrow(cv, W - M - ww, top + 26, when, mark);
  int x = M + 32;
  cv.text(font_tile, x, top + 27, c.shrt[0] ? c.shrt : c.title, Ink::Black, 0, W - M - ww - 16 - x);
}

void page_count(Canvas& cv, int n, int cur, int y) {
  if (n < 2) return;
  char buf[16];
  snprintf(buf, sizeof buf, "%d / %d", cur + 1, n);
  eyebrow(cv, W - M - eyebrow_w(buf), y, buf, Ink::Black);
}

// Clawd's pose for what is on screen. Calm cards get one of a few idle
// looks, picked by the card id so it holds still between refreshes.
Mood pick_mood(const Digest& d, const Ranked& r, const Card* c, const Status& st) {
  if (st.net == Net::NeverSynced || !st.last_sync) return Mood::Thinking;
  if (st.net == Net::Offline) return Mood::Puzzled;
  if (!c) return Mood::Happy;
  int urgent = 0;
  for (int i = 0; i < r.n; i++) urgent += d.cards[r.idx[i]].urgent;
  if (urgent >= 3) return Mood::Sweat;
  if (c->urgent) return Mood::Alert;
  int64_t dt = c->at ? c->at - st.now : 0;
  switch (c->kind) {
    case Kind::Event:
    case Kind::Quest:
      if (is_countdown(*c, st) && dt <= 5 * 60) return Mood::Surprised;
      if (is_countdown(*c, st) && dt <= 15 * 60) return Mood::Busy;
      break;
    case Kind::Commute: return strstr(c->delta, "foot") ? Mood::Walking : Mood::Skate;
    case Kind::Wins: return c->nrows >= 3 ? Mood::Celebrate : Mood::Flag;
    case Kind::Ship:
    case Kind::Db:
    case Kind::Cloud: return Mood::Builder;
    case Kind::Review: return Mood::Detective;
    case Kind::Music: return Mood::Music;
    case Kind::News:
    case Kind::Story: return Mood::Reading;
    case Kind::Focus: return Mood::Lift;
    case Kind::Parcel: return Mood::Boop;
    case Kind::Brief: return st.now && local(st.now).hour < 12 ? Mood::Coffee : Mood::Wink;
    case Kind::Claude: {
      for (int i = 0; i < c->nrows; i++)
        if (c->rows[i].tier == 0) return Mood::Wave;
      // A session still running: Clawd is at the laptop. All finished: the wizard.
      for (int i = 0; i < c->nrows; i++)
        if (!strncmp(c->rows[i].detail, "Working", 7)) return Mood::Typing;
      return Mood::Wizard;
    }
    case Kind::Money:
      if (c->delta[0] == '+') return Mood::CoinHop;
      break;
    case Kind::Traffic:
      if (c->delta[0] == '+') return Mood::Proud;
      break;
    case Kind::Weather:
      if (c->wx == Wx::Clear) return Mood::Cool;
      if (c->wx == Wx::Rain || c->wx == Wx::Storm) return Mood::Rainy;
      if (c->wx == Wx::Snow) return Mood::Cold;
      break;
    case Kind::Chat:
    case Kind::Teams:
    case Kind::Support:
    case Kind::Mail:
      // With several mail accounts the tier names the account, not the urgency.
      if (!(c->kind == Kind::Mail && c->sec[0][0]))
        for (int i = 0; i < c->nrows; i++)
          if (c->rows[i].tier == 0) return Mood::Busy;
      if (c->kind == Kind::Mail && c->nrows) return Mood::Typing;  // replies to write
      break;
    default: break;
  }
  static const Mood idle[6] = {Mood::Calm, Mood::LookLeft, Mood::LookRight, Mood::Wink, Mood::Peek, Mood::Float};
  uint32_t h = 2166136261u;
  for (const char* p = c->id; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  return idle[h % 6];
}

// Clawd standing on a rule at y, at the right margin.
void small_clawd(Canvas& cv, int rule_y, Mood mood, const Status& st) {
  int cx = W - M - clawd_w(CLAWD_CELL) - 2 * CLAWD_CELL;
  clawd(cv, cx, rule_y - clawd_h(CLAWD_CELL), CLAWD_CELL, mood, accent_ink(st));
}

// Draws the up next list at the bottom with Clawd on its rule. Returns the y
// where the hero must stop.
int up_next(Canvas& cv, const Digest& d, const Ranked& r, int cur, const Status& st, Mood mood) {
  int ids[3], n = 0;
  for (int i = 1; i < r.n && n < 3; i++) ids[n++] = r.idx[(cur + i) % r.n];
  int top = H - 8 - n * NEXT_ROW_H;
  int head = top - 14;
  int rule = n ? head - 26 : H - 12;
  cv.hline(M, rule, W - 2 * M, Ink::Black, 1);
  small_clawd(cv, rule, mood, st);
  if (n) {
    eyebrow(cv, M, head, "UP NEXT", Ink::Black);
    page_count(cv, r.n, cur, head);
    for (int i = 0; i < n; i++) {
      if (i) cv.dots(M + 32, top + i * NEXT_ROW_H, W - 2 * M - 32, 3, Ink::Black);
      next_row(cv, d.cards[ids[i]], st, top + i * NEXT_ROW_H);
    }
  }
  // Leave Clawd's height plus his mark above the rule, more under a tall hat or flag.
  int above = clawd_headroom(mood, CLAWD_CELL);
  if (above < 4 * CLAWD_CELL) above = 4 * CLAWD_CELL;
  return rule - clawd_h(CLAWD_CELL) - above - 6;
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

void big_clawd(Canvas& cv, int y, int cell, Mood mood, const Status& st) {
  clawd(cv, (W - clawd_w(cell)) / 2, y, cell, mood, accent_ink(st));
}

void next_event_footer(Canvas& cv, const Card* e, const Status& st, const char* label) {
  if (!e) return;
  int y = H - 110;
  cv.hline(M, y, W - 2 * M, Ink::Black, 1);
  char when[40];
  fmt_until(st.now, e->at, when, sizeof when);
  icon(cv, Kind::Event, M, y + 22, 20, Ink::Black);
  char lab[64];
  snprintf(lab, sizeof lab, "%s  \xC2\xB7  %s", label, when);
  eyebrow(cv, M + 32, y + 38, lab, Ink::Black, W - 2 * M - 32);
  cv.text(font_tile, M, y + 76, e->title, Ink::Black, 0, W - 2 * M);
}

void screen_empty(Canvas& cv, const Digest& d, const Status& st, uint32_t disabled) {
  bool never = st.net == Net::NeverSynced || !st.last_sync;
  big_clawd(cv, 160, 16, never ? Mood::Puzzled : Mood::Happy, st);
  center(cv, font_big, 400, never ? "Hello." : "All clear.", Ink::Black);
  const char* line = d.headline[0] ? d.headline : "Nothing needs you right now.";
  if (never) line = "Waiting for the first sync from C-Ink Settings on your computer.";
  int lines = cv.wrap(font_sub, M, 0, W - 2 * M, 36, 3, line, Ink::Black, true);
  // Center each wrapped line by drawing it into the measured width.
  int y = 452;
  if (lines == 1) center(cv, font_sub, y, line, Ink::Black);
  else cv.wrap(font_sub, M + 10, y, W - 2 * M - 20, 36, 3, line, Ink::Black);
  next_event_footer(cv, next_event(d, st.now, disabled), st, "NEXT UP");
}

void screen_night(Canvas& cv, const Digest& d, const Status& st, uint32_t disabled) {
  icon_moon(cv, W - M - 56, 64, 56, Ink::Black);
  big_clawd(cv, 210, 16, Mood::Sleepy, st);
  center(cv, font_big, 440, "Good night.", Ink::Black);
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
  cv.wrap(font_sub, M, 500, W - 2 * M, 36, 3, buf, Ink::Black);
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
  cv.text(font_body_b, M + 54, y - 6, title, Ink::Black, 0, W - M - (M + 54));
  cv.wrap(font_small, M + 54, y + 20, W - M - (M + 54), 24, 2, body, Ink::Black);
}

void screen_setup(Canvas& cv, const Status& st) {
  clawd(cv, M + 4 * 8, 70, 8, Mood::Wave, accent_ink(st));
  cv.text(font_title, M, 200, "Hi, I'm C-Ink.", Ink::Black);
  cv.wrap(font_sub, M, 244, W - 2 * M, 34, 2, "Three steps and I'll show what matters in your day.", Ink::Black);
  step(cv, 1, 350, "Get C-Ink Settings", "The small Mac / Windows app from github.com/jakehandy/c-ink.");
  step(cv, 2, 450, "Plug in with USB-C", "The app finds this TICKEY and sends it your Wi-Fi.");
  step(cv, 3, 550, "Sign in with Claude", "Pick your screens. The first sync takes about a minute.");
  cv.hline(M, H - 64, W - 2 * M, Ink::Black, 1);
  char buf[64];
  snprintf(buf, sizeof buf, "DEVICE %s", st.device_id);
  eyebrow(cv, M, H - 30, buf, Ink::Black);
  snprintf(buf, sizeof buf, "C-INK %s", st.firmware);
  eyebrow(cv, W - M - eyebrow_w(buf), H - 30, buf, Ink::Black);
}

void kv(Canvas& cv, int y, const char* k, const char* v) {
  eyebrow(cv, M, y, k, Ink::Black);
  cv.text(font_body, M + 150, y + 2, v && *v ? v : "-", Ink::Black, 0, W - 2 * M - 150);
  cv.dots(M, y + 16, W - 2 * M, 3, Ink::Black);
}

void screen_info(Canvas& cv, const Digest& d, const Status& st) {
  clawd(cv, W - M - clawd_w(6), 76, 6, Mood::Calm, accent_ink(st));
  cv.text(font_title_s, M, 112, "This TICKEY", Ink::Black);
  char buf[64], a[24];
  int y = 176;
  kv(cv, y, "DEVICE", st.device_id); y += 46;
  kv(cv, y, "FIRMWARE", st.firmware); y += 46;
  kv(cv, y, "WI-FI", st.ssid); y += 46;
  kv(cv, y, "ADDRESS", st.ip); y += 46;
  kv(cv, y, "SOURCE", st.source); y += 46;
  if (st.last_sync) { fmt_ago(st.now, st.last_sync, a, sizeof a); snprintf(buf, sizeof buf, "%s, %d cards", a, d.ncards); }
  else snprintf(buf, sizeof buf, "never");
  kv(cv, y, "LAST SYNC", buf); y += 46;
  if (st.next_sync) { fmt_clock(st.next_sync, a, sizeof a); kv(cv, y, "NEXT SYNC", a); y += 46; }
  if (st.battery >= 0) { snprintf(buf, sizeof buf, "%d%%%s", st.battery, st.charging ? ", charging" : ""); kv(cv, y, "BATTERY", buf); }
  cv.hline(M, H - 98, W - 2 * M, Ink::Black, 1);
  eyebrow(cv, M, H - 62, "UP / DOWN   BROWSE CARDS", Ink::Black);
  eyebrow(cv, M, H - 32, "HOLD BACK   SYNC NOW", Ink::Black);
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
  const Card& c = d.cards[r.idx[cur]];
  int hero_bottom = up_next(cv, d, r, cur, st, pick_mood(d, r, &c, st));
  hero(cv, d, c, st, {M, HERO_Y, W - 2 * M, hero_bottom - HERO_Y});
  return r.n;
}

}  // namespace cink
