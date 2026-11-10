#include "cink/clawd.h"

#include <string.h>

#include <initializer_list>

namespace cink {

namespace {

// The banner Claude Code prints at startup, decoded from its quadrant-block
// characters into 18 x 5 cells (1 = body). Row 1 carries the two eyes as
// gaps, row 2 the claws, row 4 the four legs.
const char* const kSprite[5] = {
    "...############...",
    "...##.######.##...",
    ".################.",
    "...############...",
    "....#.#....#.#....",
};

enum class Eye : uint8_t { Open, Shut, Smile, Lidded, Wide, Hidden };
enum class Claws : uint8_t { Out, Up, WaveR, Down, In };
enum class Legs : uint8_t { Stand, Walk };
enum class Mark : uint8_t { None, Bang, Question, Zz, Dots, Bubbles, Heart, Drop, Sparkle, Rain, Shiver, Note, Steam };
enum class Prop : uint8_t { None, Shades, Paper, Mug, Laptop, Speed };

struct Pose {
  Eye left, right;
  int8_t look;  // -1 left, 0 ahead, 1 right
  Claws claws;
  Legs legs;
  Mark mark;
  Prop prop;
};

const Pose kPoses[(int)Mood::Count] = {
    /* Calm      */ {Eye::Open, Eye::Open, 0, Claws::Out, Legs::Stand, Mark::None, Prop::None},
    /* Blink     */ {Eye::Shut, Eye::Shut, 0, Claws::Out, Legs::Stand, Mark::None, Prop::None},
    /* LookLeft  */ {Eye::Open, Eye::Open, -1, Claws::Out, Legs::Stand, Mark::None, Prop::None},
    /* LookRight */ {Eye::Open, Eye::Open, 1, Claws::Out, Legs::Stand, Mark::None, Prop::None},
    /* Wink      */ {Eye::Open, Eye::Shut, 0, Claws::Out, Legs::Stand, Mark::None, Prop::None},
    /* Happy     */ {Eye::Smile, Eye::Smile, 0, Claws::Up, Legs::Stand, Mark::None, Prop::None},
    /* Wave      */ {Eye::Open, Eye::Smile, 0, Claws::WaveR, Legs::Stand, Mark::None, Prop::None},
    /* Alert     */ {Eye::Wide, Eye::Wide, 0, Claws::Out, Legs::Stand, Mark::Bang, Prop::None},
    /* Surprised */ {Eye::Wide, Eye::Wide, 0, Claws::Up, Legs::Stand, Mark::None, Prop::None},
    /* Puzzled   */ {Eye::Open, Eye::Lidded, 1, Claws::Out, Legs::Stand, Mark::Question, Prop::None},
    /* Sleepy    */ {Eye::Shut, Eye::Shut, 0, Claws::In, Legs::Stand, Mark::Zz, Prop::None},
    /* Busy      */ {Eye::Open, Eye::Open, 1, Claws::Out, Legs::Stand, Mark::Dots, Prop::None},
    /* Thinking  */ {Eye::Lidded, Eye::Lidded, -1, Claws::In, Legs::Stand, Mark::Bubbles, Prop::None},
    /* Focused   */ {Eye::Lidded, Eye::Lidded, 0, Claws::Down, Legs::Stand, Mark::None, Prop::None},
    /* Love      */ {Eye::Smile, Eye::Smile, 0, Claws::In, Legs::Stand, Mark::Heart, Prop::None},
    /* Sweat     */ {Eye::Wide, Eye::Wide, 0, Claws::Out, Legs::Stand, Mark::Drop, Prop::None},
    /* Proud     */ {Eye::Smile, Eye::Smile, 0, Claws::Up, Legs::Stand, Mark::Sparkle, Prop::None},
    /* Cool      */ {Eye::Hidden, Eye::Hidden, 0, Claws::Out, Legs::Stand, Mark::None, Prop::Shades},
    /* Rainy     */ {Eye::Lidded, Eye::Lidded, 0, Claws::In, Legs::Stand, Mark::Rain, Prop::None},
    /* Cold      */ {Eye::Shut, Eye::Shut, 0, Claws::In, Legs::Stand, Mark::Shiver, Prop::None},
    /* Reading   */ {Eye::Lidded, Eye::Lidded, 0, Claws::Out, Legs::Stand, Mark::None, Prop::Paper},
    /* Coffee    */ {Eye::Smile, Eye::Smile, 0, Claws::Out, Legs::Stand, Mark::Steam, Prop::Mug},
    /* Walking   */ {Eye::Open, Eye::Open, 1, Claws::Out, Legs::Walk, Mark::None, Prop::Speed},
    /* Music     */ {Eye::Open, Eye::Shut, 0, Claws::WaveR, Legs::Stand, Mark::Note, Prop::None},
    /* Typing    */ {Eye::Open, Eye::Open, 0, Claws::Down, Legs::Stand, Mark::None, Prop::Laptop},
};

const char* const kNames[(int)Mood::Count] = {
    "calm", "blink", "look-left", "look-right", "wink", "happy", "wave", "alert", "surprised",
    "puzzled", "sleepy", "busy", "thinking", "focused", "love", "sweat", "proud", "cool",
    "rainy", "cold", "reading", "coffee", "walking", "music", "typing"};

}  // namespace

const char* mood_name(Mood m) { return (int)m < (int)Mood::Count ? kNames[(int)m] : "calm"; }

void clawd(Canvas& cv, int x, int y, int cell, Mood mood, Ink body) {
  const Pose& p = kPoses[(int)mood < (int)Mood::Count ? (int)mood : 0];
  const int cw = cell, ch = cell * 2;
  auto blk = [&](int c, int r, Ink ink) { cv.fill(x + c * cw, y + r * ch, cw, ch, ink); };

  // Body and legs, eyes filled in; eyes are cut back out below.
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 18; c++) {
      bool on = kSprite[r][c] == '#' || (r == 1 && (c == 5 || c == 12));
      if (r == 2 && (c <= 2 || c >= 15)) on = false;  // claws per pose
      if (r == 4) on = false;                         // legs per pose
      if (on) blk(c, r, body);
    }
  switch (p.claws) {
    case Claws::Out:
      for (int c : {1, 2, 15, 16}) blk(c, 2, body);
      break;
    case Claws::Up:
      blk(2, 2, body); blk(15, 2, body);
      cv.fill(x + 1 * cw, y, cw, ch * 2, body);
      cv.fill(x + 2 * cw, y + ch, cw, ch, body);
      cv.fill(x + 16 * cw, y, cw, ch * 2, body);
      cv.fill(x + 15 * cw, y + ch, cw, ch, body);
      break;
    case Claws::WaveR:
      for (int c : {1, 2, 15}) blk(c, 2, body);
      cv.fill(x + 16 * cw, y, cw, ch * 2, body);
      cv.fill(x + 15 * cw, y + ch, cw, ch, body);
      break;
    case Claws::Down:
      blk(2, 2, body); blk(15, 2, body);
      blk(2, 3, body); blk(15, 3, body);
      break;
    case Claws::In:
      blk(2, 2, body); blk(15, 2, body);
      break;
  }
  if (p.legs == Legs::Stand) {
    for (int c : {4, 6, 11, 13}) blk(c, 4, body);
  } else {
    for (int c : {3, 6, 11, 14}) blk(c, 4, body);  // mid-stride, legs splayed
  }

  // Eyes: holes in row 1, at columns 5 and 12, shifted when looking aside.
  const int ex[2] = {5 + p.look, 12 + p.look};
  const Eye eyes[2] = {p.left, p.right};
  const int slit = ch / 4 > 1 ? ch / 4 : 1;
  for (int i = 0; i < 2; i++) {
    int x0 = x + ex[i] * cw, y0 = y + ch;
    switch (eyes[i]) {
      case Eye::Open: cv.fill(x0, y0, cw, ch, Ink::White); break;
      case Eye::Shut: cv.fill(x0, y0 + ch - slit * 2, cw, slit, Ink::White); break;
      case Eye::Smile: cv.fill(x0, y0, cw, ch / 2, Ink::White); break;      // eye crinkled shut from below
      case Eye::Lidded: cv.fill(x0, y0 + ch / 2, cw, ch / 2, Ink::White); break;
      case Eye::Wide: cv.fill(x0, y0 - ch / 2, cw, ch + ch / 2, Ink::White); break;
      case Eye::Hidden: break;
    }
  }

  // Props, in black so they read on the accent body.
  const int u = cell < 4 ? (cell < 3 ? 1 : 2) : cell / 2;  // stroke unit
  switch (p.prop) {
    case Prop::Shades:
      cv.fill(x + 4 * cw, y + ch + ch / 4, 10 * cw, ch / 2, Ink::Black);
      cv.fill(x + 4 * cw, y + ch + ch / 4, 3 * cw, ch - ch / 4, Ink::Black);
      cv.fill(x + 11 * cw, y + ch + ch / 4, 3 * cw, ch - ch / 4, Ink::Black);
      break;
    case Prop::Paper: {
      // A broadsheet held up in front of the claws, with a headline bar.
      int px = x + 1 * cw, py = y + 2 * ch - ch / 4, pw = 16 * cw, ph = ch + ch / 2;
      cv.fill(px, py, pw, ph, Ink::White);
      cv.rect(px, py, pw, ph, Ink::Black, u);
      cv.fill(px + 2 * cw, py + u * 2, pw - 4 * cw, u * 2, Ink::Black);
      cv.hline(px + 2 * cw, py + ph - u * 3, pw / 2 - 3 * cw, Ink::Black, u);
      cv.hline(px + pw / 2 + cw, py + ph - u * 3, pw / 2 - 3 * cw, Ink::Black, u);
      break;
    }
    case Prop::Mug: {
      int mx = x + 17 * cw, my = y + 2 * ch - ch / 2;
      cv.rect(mx, my, 3 * cw, ch + ch / 2, Ink::Black, u);
      cv.rect(mx + 3 * cw - u, my + ch / 4, cw + u, ch / 2 + u, Ink::Black, u);
      break;
    }
    case Prop::Laptop: {
      int lx = x + 4 * cw, ly = y + 2 * ch + ch / 2;
      cv.fill(lx, ly, 10 * cw, ch, Ink::White);
      cv.rect(lx, ly, 10 * cw, ch, Ink::Black, u);
      cv.fill(lx - cw, ly + ch, 12 * cw, u * 2, Ink::Black);
      break;
    }
    case Prop::Speed:
      for (int i = 0; i < 3; i++) cv.hline(x - (3 + i % 2) * cw, y + ch / 2 + i * ch, (2 + i % 2) * cw, Ink::Black, u);
      break;
    default: break;
  }

  // Marks sit above the right side of the head and need 3 cells of height.
  const int mx = x + 14 * cw, top = y - 3 * cw - 4;
  switch (p.mark) {
    case Mark::Bang: {
      int h = 3 * cw;
      cv.fill(mx + cw, top, u * 2, h - u * 3, Ink::Black);
      cv.fill(mx + cw, top + h - u * 2, u * 2, u * 2, Ink::Black);
      break;
    }
    case Mark::Question: {
      int r = cw + u;
      int cx = mx + cw + r, cy = top + r;
      cv.circle(cx, cy, r, Ink::Black, u);
      cv.fill(cx - r - 1, cy, r + 1, r + 2, Ink::White);  // open the lower left of the hook
      cv.fill(cx - u / 2, cy + r - u, u, u * 2, Ink::Black);
      cv.fill(cx - u / 2, top + 3 * cw - u, u, u, Ink::Black);
      break;
    }
    case Mark::Zz:
      for (int i = 0; i < 2; i++) {
        int zs = cw * (i ? 3 : 2), zx = mx + i * cw * 3, zy = top + 3 * cw;
        cv.hline(zx, zy - zs, zs, Ink::Black, u);
        cv.line(zx + zs - 1, zy - zs + u / 2, zx, zy - 1 - u / 2, Ink::Black, u);
        cv.hline(zx, zy - u, zs, Ink::Black, u);
      }
      break;
    case Mark::Dots:
      for (int i = 0; i < 3; i++) cv.circle_fill(mx + cw + i * (u * 4), top + 3 * cw - u * 2, u, Ink::Black);
      break;
    case Mark::Bubbles:
      cv.circle_fill(mx, top + 3 * cw - u, u, Ink::Black);
      cv.circle(mx + 2 * cw, top + 2 * cw - u, u + 1, Ink::Black, u > 1 ? u - 1 : 1);
      cv.circle(mx + 4 * cw, top + cw / 2 + u, u * 2, Ink::Black, u > 1 ? u - 1 : 1);
      break;
    case Mark::Heart: {
      int r = cw * 3 / 4 + 1, hx = mx + cw + r, hy = top + r;
      cv.circle_fill(hx, hy, r, Ink::Black);
      cv.circle_fill(hx + 2 * r, hy, r, Ink::Black);
      for (int i = 0; i <= 2 * r; i++) cv.hline(hx - r + i, hy + i, 4 * r - 2 * i + 1, Ink::Black);
      break;
    }
    case Mark::Drop: {
      // On the left of the head, falling.
      int dx = x + 2 * cw, dy = y - cw;
      cv.line(dx, dy - 2 * cw, dx - cw, dy, Ink::Black, u);
      cv.line(dx, dy - 2 * cw, dx + cw, dy, Ink::Black, u);
      cv.circle_fill(dx, dy, cw, Ink::Black);
      break;
    }
    case Mark::Sparkle: {
      int cx = mx + 2 * cw, cy = top + 3 * cw / 2, r = 3 * cw / 2 + u;
      cv.fill(cx - u / 2, cy - r, u + (u & 1 ? 0 : 1), 2 * r, Ink::Black);
      cv.fill(cx - r, cy - u / 2, 2 * r, u + (u & 1 ? 0 : 1), Ink::Black);
      cv.fill(cx - u, cy - u, u * 2, u * 2, Ink::Black);
      break;
    }
    case Mark::Rain:
      for (int i = 0; i < 4; i++) {
        int rx = x + (3 + i * 4) * cw, ry = top + (i % 2) * cw;
        cv.line(rx + cw, ry, rx, ry + 2 * cw, Ink::Black, u);
      }
      break;
    case Mark::Shiver:
      for (int side = 0; side < 2; side++) {
        int sx = side ? x + 18 * cw + u : x - 2 * u - cw;
        for (int i = 0; i < 3; i++) cv.vline(sx + (i % 2) * u * 2, y + ch / 2 + i * (ch * 2 / 3), ch / 2, Ink::Black, u);
      }
      break;
    case Mark::Note: {
      int nx = mx + cw, ny = top + 3 * cw;
      cv.circle_fill(nx + cw / 2, ny - cw / 2, cw / 2 + u / 2, Ink::Black);
      cv.vline(nx + cw, ny - 3 * cw, 5 * cw / 2, Ink::Black, u);
      cv.line(nx + cw, ny - 3 * cw, nx + 3 * cw, ny - 2 * cw, Ink::Black, u);
      break;
    }
    case Mark::Steam: {
      int sx = x + 18 * cw;
      for (int i = 0; i < 2; i++) {
        int bx = sx + i * cw * 3 / 2;
        cv.line(bx, y + ch / 2, bx + cw / 2, y, Ink::Black, u);
        cv.line(bx + cw / 2, y, bx, y - ch / 2, Ink::Black, u);
      }
      break;
    }
    default: break;
  }
}

}  // namespace cink
