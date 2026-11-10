#include "cink/clawd.h"

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

}  // namespace

void clawd(Canvas& cv, int x, int y, int cell, Mood mood, Ink body) {
  const int cw = cell, ch = cell * 2;
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 18; c++) {
      if (kSprite[r][c] != '#') continue;
      bool claw = r == 2 && (c <= 2 || c >= 15);
      if (claw && mood == Mood::Happy) continue;  // drawn raised below
      cv.fill(x + c * cw, y + r * ch, cw, ch, body);
    }
  // Happy: claws raised beside the head instead of out to the sides.
  if (mood == Mood::Happy) {
    cv.fill(x + 1 * cw, y + 0 * ch, cw, ch * 2, body);
    cv.fill(x + 2 * cw, y + 1 * ch, cw, ch, body);
    cv.fill(x + 16 * cw, y + 0 * ch, cw, ch * 2, body);
    cv.fill(x + 15 * cw, y + 1 * ch, cw, ch, body);
  }
  // Sleepy: eyes shut, a lid line low in each eye cell.
  if (mood == Mood::Sleepy) {
    int lid = ch / 4 > 1 ? ch / 4 : 1;
    cv.fill(x + 5 * cw, y + ch, cw, ch - lid * 2, body);
    cv.fill(x + 12 * cw, y + ch, cw, ch - lid * 2, body);
    cv.fill(x + 5 * cw, y + 2 * ch - lid, cw, lid, body);
    cv.fill(x + 12 * cw, y + 2 * ch - lid, cw, lid, body);
  }

  // Mood marks sit above the right side of the head, in black so they read
  // even when the body is in the accent color. They need 3 cells of height.
  const int mx = x + 14 * cw, top = y - 3 * cw - 4;
  const int u = cell < 4 ? 2 : cell / 2;  // stroke unit
  switch (mood) {
    case Mood::Alert: {
      int h = 3 * cw;
      cv.fill(mx + cw, top, u * 2, h - u * 3, Ink::Black);
      cv.fill(mx + cw, top + h - u * 2, u * 2, u * 2, Ink::Black);
      break;
    }
    case Mood::Puzzled: {
      int r = cw + u;
      int cx = mx + cw + r, cy = top + r;
      cv.circle(cx, cy, r, Ink::Black, u);
      cv.fill(cx - r - 1, cy, r + 1, r + 2, Ink::White);  // open the lower left of the hook
      cv.fill(cx - u / 2, cy + r - u, u, u * 2, Ink::Black);
      cv.fill(cx - u / 2, top + 3 * cw - u, u, u, Ink::Black);
      break;
    }
    case Mood::Sleepy:
      for (int i = 0; i < 2; i++) {
        int zs = cw * (i ? 3 : 2), zx = mx + i * cw * 3, zy = top + 3 * cw;
        cv.hline(zx, zy - zs, zs, Ink::Black, u);
        cv.line(zx + zs - 1, zy - zs + u / 2, zx, zy - 1 - u / 2, Ink::Black, u);
        cv.hline(zx, zy - u, zs, Ink::Black, u);
      }
      break;
    case Mood::Busy:
      for (int i = 0; i < 3; i++) cv.circle_fill(mx + cw + i * (u * 4), top + 3 * cw - u * 2, u, Ink::Black);
      break;
    default:
      break;
  }
}

}  // namespace cink
