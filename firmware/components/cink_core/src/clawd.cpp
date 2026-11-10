#include "cink/clawd.h"

#include <string.h>

namespace cink {

namespace {

// Claude Code draws Clawd with quadrant-block characters: 9 columns by 3
// rows. Each character here is one hex digit of its filled quadrants
// (1 top-left, 2 top-right, 4 bottom-left, 8 bottom-right), so '7' is the
// eye glyph with its bottom-right quadrant open. 'q' is a lower-quarter
// block, used inverted as a closed eyelid. A character is 2 x 4 sprite
// pixels, which keeps the pixels square at the terminal's 1:2 cell shape.

// Arms: row 0 columns 0-1 and 8, row 1 columns 0-1 and 7-8.
struct Arms { const char *r1L, *r1R, *r2L, *r2R; };
const Arms kArmsDown = {"0A", "0", "2B", "F3"};
const Arms kArmsUp = {"8E", "C", "0B", "F1"};
const Arms kArmsOneUp = {"0A", "C", "2B", "F1"};

// Eyes: row 0 columns 2-7. 'L' marks a lid: the cell is body with the glyph cut out.
struct Eyes { const char *glyphs, *lids; };
const Eyes kEyesOpen = {"7FFF7F", "......"};
const Eyes kEyesLeft = {"EFFFEF", "......"};
const Eyes kEyesRight = {"FEFFFE", "......"};
const Eyes kEyesClosed = {"qFFFqF", "L...L."};
const Eyes kEyesWink = {"7FFFqF", "....L."};

const char* const kFeetBoth = "022000220";
const char* const kFeetLeft = "022000000";
const char* const kFeetRight = "000000220";

// Turned frames: three rows, and the span [from, to) whose open quadrants
// show the background (eyes and the gaps between legs).
struct Facing { const char* rows[3]; uint8_t from[2], to[2]; };
const Facing kRight30 = {{"00F7FF750", "02FFFFF70", "001100110"}, {2, 2}, {7, 7}};
const Facing kRight55 = {{"00AF7FB00", "00AFFFF00", "002202200"}, {3, 3}, {7, 7}};
const Facing kBack = {{"0AFFFFFF0", "2BFFFFFF3", "022000220"}, {2, 2}, {8, 7}};

enum class Mark : uint8_t { None, Bang, Question, Zz, Dots, Bubbles, Heart, Drop, Sparkle, Rain, Shiver, Note, Steam, Sparkles };
enum class Prop : uint8_t { None, Shades, Paper, Mug, Laptop, Speed };
enum class Poof : uint8_t { None, Dot, Wave };

struct Pose {
  const Eyes* eyes;
  const Arms* arms;
  const char* feet;
  const Facing* facing;  // overrides eyes, arms and feet
  int8_t lift;           // sprite pixels: negative hops up, positive crouches
  Poof poof;
  bool shadow;
  Mark mark;
  Prop prop;
};

const Pose kPoses[(int)Mood::Count] = {
    /* Calm      */ {&kEyesOpen, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::None},
    /* Blink     */ {&kEyesClosed, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::None},
    /* LookLeft  */ {&kEyesLeft, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::None},
    /* LookRight */ {&kEyesRight, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::None},
    /* Wink      */ {&kEyesWink, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::None},
    /* Happy     */ {&kEyesOpen, &kArmsUp, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::None},
    /* Wave      */ {&kEyesOpen, &kArmsOneUp, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::None},
    /* Alert     */ {&kEyesOpen, &kArmsUp, kFeetBoth, nullptr, 0, Poof::None, false, Mark::Bang, Prop::None},
    /* Surprised */ {&kEyesOpen, &kArmsUp, kFeetBoth, nullptr, -3, Poof::Dot, false, Mark::None, Prop::None},
    /* Puzzled   */ {&kEyesLeft, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::Question, Prop::None},
    /* Sleepy    */ {&kEyesClosed, &kArmsDown, kFeetBoth, nullptr, 2, Poof::None, false, Mark::Zz, Prop::None},
    /* Busy      */ {&kEyesRight, &kArmsDown, kFeetLeft, nullptr, 0, Poof::None, false, Mark::Dots, Prop::None},
    /* Thinking  */ {&kEyesLeft, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::Bubbles, Prop::None},
    /* Focused   */ {nullptr, nullptr, nullptr, &kBack, 0, Poof::None, false, Mark::None, Prop::None},
    /* Love      */ {&kEyesClosed, &kArmsUp, kFeetBoth, nullptr, 0, Poof::None, false, Mark::Heart, Prop::None},
    /* Sweat     */ {&kEyesOpen, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::Drop, Prop::None},
    /* Proud     */ {&kEyesOpen, &kArmsUp, kFeetBoth, nullptr, 0, Poof::None, false, Mark::Sparkle, Prop::None},
    /* Cool      */ {&kEyesOpen, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::Shades},
    /* Rainy     */ {&kEyesClosed, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::Rain, Prop::None},
    /* Cold      */ {&kEyesClosed, &kArmsUp, kFeetBoth, nullptr, 1, Poof::Wave, false, Mark::Shiver, Prop::None},
    /* Reading   */ {&kEyesOpen, &kArmsUp, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::Paper},
    /* Coffee    */ {&kEyesOpen, &kArmsOneUp, kFeetBoth, nullptr, 0, Poof::None, false, Mark::Steam, Prop::Mug},
    /* Walking   */ {nullptr, nullptr, nullptr, &kRight30, 0, Poof::None, false, Mark::None, Prop::Speed},
    /* Music     */ {&kEyesClosed, &kArmsOneUp, kFeetRight, nullptr, 0, Poof::None, false, Mark::Note, Prop::None},
    /* Typing    */ {&kEyesOpen, &kArmsDown, kFeetBoth, nullptr, 0, Poof::None, false, Mark::None, Prop::Laptop},
    /* Celebrate */ {&kEyesOpen, &kArmsUp, kFeetBoth, nullptr, 0, Poof::Wave, false, Mark::Sparkles, Prop::None},
    /* Boop      */ {&kEyesClosed, &kArmsDown, kFeetBoth, nullptr, 1, Poof::Dot, false, Mark::None, Prop::None},
    /* CoinHop   */ {&kEyesOpen, &kArmsUp, kFeetBoth, nullptr, -3, Poof::None, true, Mark::None, Prop::None},
    /* Crouch    */ {&kEyesRight, &kArmsDown, kFeetBoth, nullptr, 2, Poof::None, false, Mark::None, Prop::None},
    /* Peek      */ {nullptr, nullptr, nullptr, &kRight55, 0, Poof::None, false, Mark::None, Prop::None},
};

const char* const kNames[(int)Mood::Count] = {
    "calm", "blink", "look-left", "look-right", "wink", "happy", "wave", "alert", "surprised", "puzzled",
    "sleepy", "busy", "thinking", "focused", "love", "sweat", "proud", "cool", "rainy", "cold",
    "reading", "coffee", "walking", "music", "typing", "celebrate", "boop", "coin-hop", "crouch", "peek"};

int quad(char g) {
  if (g >= '0' && g <= '9') return g - '0';
  if (g >= 'A' && g <= 'F') return g - 'A' + 10;
  return 0;
}

// One character cell at (x, y): 2 x 4 sprite pixels of size u.
void glyph(Canvas& cv, int x, int y, int u, char g, Ink ink) {
  if (g == 'q') { cv.fill(x, y + 3 * u, 2 * u, u, ink); return; }
  if (g == 'e') { cv.fill(x, y + 3 * u + u / 2, 2 * u, u - u / 2, ink); return; }
  int q = quad(g);
  if (q & 1) cv.fill(x, y, u, 2 * u, ink);
  if (q & 2) cv.fill(x + u, y, u, 2 * u, ink);
  if (q & 4) cv.fill(x, y + 2 * u, u, 2 * u, ink);
  if (q & 8) cv.fill(x + u, y + 2 * u, u, 2 * u, ink);
}

// A run of characters starting at column col. hole: paint the cells white
// first so open quadrants read as background. lid: the cell is body and the
// glyph is cut out of it.
void run(Canvas& cv, int x, int y, int u, int col, int row, const char* s, Ink body, bool hole = false,
         const char* lids = nullptr) {
  for (int i = 0; s[i]; i++) {
    int cx = x + (col + i) * 2 * u, cy = y + row * 4 * u;
    bool lid = lids && lids[i] == 'L';
    if (lid) {
      cv.fill(cx, cy, 2 * u, 4 * u, body);
      glyph(cv, cx, cy, u, s[i], Ink::White);
      continue;
    }
    if (hole) cv.fill(cx, cy, 2 * u, 4 * u, Ink::White);
    glyph(cv, cx, cy, u, s[i], body);
  }
}

}  // namespace

const char* mood_name(Mood m) { return (int)m < (int)Mood::Count ? kNames[(int)m] : "calm"; }

void clawd(Canvas& cv, int x, int y, int cell, Mood mood, Ink body) {
  const Pose& p = kPoses[(int)mood < (int)Mood::Count ? (int)mood : 0];
  const int u = cell;
  const int base = y;  // where the feet stand: y + 10u
  const int sy = y + p.lift * u;
  const bool crouch = p.lift > 0;

  if (p.facing) {
    const Facing& f = *p.facing;
    for (int r = 0; r < 2; r++) {
      const char* s = f.rows[r];
      for (int c = 0; s[c]; c++) {
        bool hole = c >= f.from[r] && c < f.to[r];
        char one[2] = {s[c], 0};
        run(cv, x, sy, u, c, r, one, body, hole);
      }
    }
    if (!crouch) run(cv, x, sy, u, 0, 2, f.rows[2], body);
  } else {
    const Arms& a = *p.arms;
    run(cv, x, sy, u, 0, 0, a.r1L, body);
    run(cv, x, sy, u, 2, 0, p.eyes->glyphs, body, true, p.eyes->lids);
    run(cv, x, sy, u, 8, 0, a.r1R, body);
    run(cv, x, sy, u, 0, 1, a.r2L, body);
    run(cv, x, sy, u, 2, 1, "FFFFF", body);
    run(cv, x, sy, u, 7, 1, a.r2R, body);
    // Crouched, the legs fold away under the body, as in Claude Code.
    if (!crouch) run(cv, x, sy, u, 0, 2, p.feet, body);
  }

  // Strokes for marks and props, in black so they read on the accent body.
  const int t = u < 4 ? (u < 3 ? 1 : 2) : u / 2;

  if (p.shadow) cv.dots(x + 5 * u, base + 10 * u - t, 8 * u, 2, Ink::Black);
  if (p.poof != Poof::None) {
    // Either side, at foot level, where Claude Code puts its "·" and "~".
    for (int side = 0; side < 2; side++) {
      int px = side ? x + 19 * u : x - 2 * u - t, py = base + 8 * u;
      if (p.poof == Poof::Dot) {
        cv.fill(px, py, 2 * t, 2 * t, Ink::Black);
      } else {
        cv.line(px - u, py + u, px, py, Ink::Black, t);
        cv.line(px, py, px + u, py + u, Ink::Black, t);
        cv.line(px + u, py + u, px + 2 * u, py, Ink::Black, t);
      }
    }
  }

  switch (p.prop) {
    case Prop::Shades:
      cv.fill(x + 3 * u, sy + 2 * u, 12 * u, u, Ink::Black);
      cv.fill(x + 4 * u, sy + 2 * u, 4 * u, 2 * u, Ink::Black);
      cv.fill(x + 11 * u, sy + 2 * u, 4 * u, 2 * u, Ink::Black);
      break;
    case Prop::Paper: {
      // Peeking over a broadsheet held up in both claws.
      int px = x + u, py = sy + 4 * u, pw = 16 * u, ph = 5 * u;
      cv.fill(px, py, pw, ph, Ink::White);
      cv.rect(px, py, pw, ph, Ink::Black, t);
      cv.fill(px + 2 * u, py + u + t, 5 * u, u, Ink::Black);  // masthead
      for (int i = 0; i < 2; i++) {
        cv.hline(px + 2 * u, py + 3 * u + i * u, 5 * u, Ink::Black, t);
        cv.hline(px + 9 * u, py + u + t + i * u * 3 / 2, 5 * u, Ink::Black, t);
      }
      cv.vline(px + pw / 2, py + t, ph - 2 * t, Ink::Black, t);  // the fold
      break;
    }
    case Prop::Mug: {
      // Held up in the raised claw.
      int mx = x + 15 * u + u / 2, my = sy - u;
      cv.fill(mx, my, 3 * u, 3 * u, Ink::White);
      cv.rect(mx, my, 3 * u, 3 * u, Ink::Black, t);
      cv.rect(mx + 3 * u - t, my + u / 2, u + t, 3 * u / 2, Ink::Black, t);
      break;
    }
    case Prop::Laptop: {
      int lx = x + 3 * u, ly = sy + 5 * u;
      cv.fill(lx, ly, 12 * u, 4 * u, Ink::White);
      cv.rect(lx, ly, 12 * u, 4 * u, Ink::Black, t);
      cv.fill(lx + 5 * u, ly + 3 * u / 2, 2 * u, u, Ink::Black);  // a mark on the lid
      cv.fill(lx - u, ly + 4 * u, 14 * u, t * 2, Ink::Black);
      break;
    }
    case Prop::Speed:
      for (int i = 0; i < 3; i++) cv.hline(x + (i % 2 ? 0 : u), sy + u + i * 3 * u, (2 + i % 2) * u, Ink::Black, t);
      break;
    default: break;
  }

  // Marks sit above the right side of the head, up to 4 pixels high.
  const int mx = x + 14 * u, top = sy - 4 * u;
  switch (p.mark) {
    case Mark::Bang:
      cv.fill(mx + u, top, 2 * t, 3 * u - t, Ink::Black);
      cv.fill(mx + u, top + 3 * u + t, 2 * t, 2 * t, Ink::Black);
      break;
    case Mark::Question: {
      int r = u + t, cx = mx + u + r, cy = top + r;
      cv.circle(cx, cy, r, Ink::Black, t);
      cv.fill(cx - r - 1, cy, r + 1, r + 2, Ink::White);  // open the lower left of the hook
      cv.fill(cx - t / 2, cy + r - t, t, 2 * t, Ink::Black);
      cv.fill(cx - t / 2, top + 3 * u + t, t, t, Ink::Black);
      break;
    }
    case Mark::Zz:
      for (int i = 0; i < 2; i++) {
        int zs = u * (i ? 3 : 2), zx = mx + i * u * 3, zy = top + 3 * u + (i ? -u : 0);
        cv.hline(zx, zy - zs, zs, Ink::Black, t);
        cv.line(zx + zs - 1, zy - zs + t / 2, zx, zy - 1 - t / 2, Ink::Black, t);
        cv.hline(zx, zy - t, zs, Ink::Black, t);
      }
      break;
    case Mark::Dots:
      for (int i = 0; i < 3; i++) cv.circle_fill(mx + u + i * (t * 4), top + 3 * u, t, Ink::Black);
      break;
    case Mark::Bubbles:
      cv.circle_fill(mx, top + 3 * u, t, Ink::Black);
      cv.circle(mx + 2 * u, top + 2 * u, t + 1, Ink::Black, t > 1 ? t - 1 : 1);
      cv.circle(mx + 4 * u, top + u / 2, t * 2, Ink::Black, t > 1 ? t - 1 : 1);
      break;
    case Mark::Heart: {
      int r = u * 3 / 4 + 1, hx = mx + u + r, hy = top + r;
      cv.circle_fill(hx, hy, r, Ink::Black);
      cv.circle_fill(hx + 2 * r, hy, r, Ink::Black);
      for (int i = 0; i <= 2 * r; i++) cv.hline(hx - r + i, hy + i, 4 * r - 2 * i + 1, Ink::Black);
      break;
    }
    case Mark::Drop: {
      int dx = x + 2 * u, dy = sy - u;
      cv.line(dx, dy - 2 * u, dx - u, dy, Ink::Black, t);
      cv.line(dx, dy - 2 * u, dx + u, dy, Ink::Black, t);
      cv.circle_fill(dx, dy, u, Ink::Black);
      break;
    }
    case Mark::Sparkle:
    case Mark::Sparkles: {
      auto star = [&](int cx, int cy, int r) {
        cv.fill(cx - t / 2, cy - r, t + (t & 1 ? 0 : 1), 2 * r, Ink::Black);
        cv.fill(cx - r, cy - t / 2, 2 * r, t + (t & 1 ? 0 : 1), Ink::Black);
        cv.fill(cx - t, cy - t, t * 2, t * 2, Ink::Black);
      };
      star(mx + 2 * u, top + 3 * u / 2, 3 * u / 2 + t);
      if (p.mark == Mark::Sparkles) star(x + 2 * u, top + 2 * u, u + t);
      break;
    }
    case Mark::Rain:
      for (int i = 0; i < 4; i++) {
        int rx = x + (3 + i * 4) * u, ry = top + (i % 2) * u;
        cv.line(rx + u, ry, rx, ry + 2 * u, Ink::Black, t);
      }
      break;
    case Mark::Shiver:
      for (int side = 0; side < 2; side++) {
        int sx = side ? x + 18 * u + t : x - 2 * t - u;
        for (int i = 0; i < 3; i++) cv.vline(sx + (i % 2) * t * 2, sy + u + i * 2 * u, u, Ink::Black, t);
      }
      break;
    case Mark::Note: {
      int nx = mx + u, ny = top + 3 * u;
      cv.circle_fill(nx + u / 2, ny - u / 2, u / 2 + t / 2, Ink::Black);
      cv.vline(nx + u, ny - 3 * u, 5 * u / 2, Ink::Black, t);
      cv.line(nx + u, ny - 3 * u, nx + 3 * u, ny - 2 * u, Ink::Black, t);
      break;
    }
    case Mark::Steam: {
      int sx = x + 16 * u;
      for (int i = 0; i < 2; i++) {
        int bx = sx + i * u * 3 / 2;
        cv.line(bx, sy - 2 * u, bx + u / 2, sy - 3 * u, Ink::Black, t);
        cv.line(bx + u / 2, sy - 3 * u, bx, sy - 4 * u, Ink::Black, t);
      }
      break;
    }
    default: break;
  }
}

}  // namespace cink
