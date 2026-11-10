#include "cink/icons.h"

namespace cink {

void icon(Canvas& c, Kind k, int x, int y, int s, Ink ink) {
  int t = s >= 28 ? 3 : 2;
  auto P = [&](int pct) { return pct * s / 100; };
  switch (k) {
    case Kind::Event:
      c.round_rect(x, y + P(12), s, s - P(12), P(14), ink, t);
      c.fill(x, y + P(12), s, P(30), ink);
      c.fill(x + P(25), y, t, P(24), ink);
      c.fill(x + P(75) - t, y, t, P(24), ink);
      c.fill(x + P(25), y + P(58), P(16), P(14), ink);
      break;
    case Kind::Review:
      c.circle(x + P(22), y + P(16), P(15), ink, t);
      c.circle(x + P(22), y + P(84), P(15), ink, t);
      c.circle(x + P(78), y + P(84), P(15), ink, t);
      c.vline(x + P(22) - t / 2, y + P(31), P(38), ink, t);
      c.vline(x + P(78) - t / 2, y + P(36), P(33), ink, t);
      c.line(x + P(78), y + P(36), x + P(58), y + P(16), ink, t);
      c.line(x + P(58), y + P(16), x + P(66), y + P(8), ink, t);
      c.line(x + P(58), y + P(16), x + P(66), y + P(24), ink, t);
      break;
    case Kind::Chat:
      c.round_rect(x, y, s, P(74), P(22), ink, t);
      c.line(x + P(24), y + P(72), x + P(14), y + P(98), ink, t);
      c.line(x + P(14), y + P(98), x + P(46), y + P(72), ink, t);
      break;
    case Kind::Mail:
      c.rect(x, y + P(15), s, P(70), ink, t);
      c.line(x, y + P(15), x + P(50), y + P(55), ink, t);
      c.line(x + s - 1, y + P(15), x + P(50), y + P(55), ink, t);
      break;
    case Kind::Task:
      c.circle(x + s / 2, y + s / 2, s / 2, ink, t);
      c.line(x + P(28), y + P(52), x + P(44), y + P(68), ink, t + 1);
      c.line(x + P(44), y + P(68), x + P(74), y + P(34), ink, t + 1);
      break;
    case Kind::Ship:
      c.line(x + P(50), y, x + P(4), y + P(90), ink, t);
      c.line(x + P(50), y, x + P(96), y + P(90), ink, t);
      c.hline(x + P(4), y + P(90) - t / 2, P(92), ink, t);
      c.vline(x + P(50) - t / 2, y + P(34), P(30), ink, t);
      c.fill(x + P(50) - t / 2, y + P(72), t, t, ink);
      break;
    case Kind::Money: {
      c.circle(x + s / 2, y + s / 2, s / 2, ink, t);
      const Font& f = s >= 28 ? font_body_b : font_caption;
      int w = Canvas::measure(f, "$");
      c.text(f, x + (s - w) / 2, y + s / 2 + f.size * 36 / 100, "$", ink);
      break;
    }
    case Kind::Music:
      c.circle_fill(x + P(28), y + P(78), P(18), ink);
      c.vline(x + P(42), y + P(6), P(72), ink, t);
      c.line(x + P(42), y + P(6), x + P(86), y + P(22), ink, t);
      c.line(x + P(86), y + P(22), x + P(86), y + P(40), ink, t);
      break;
    case Kind::Quest: {
      int cx = x + s / 2, r = s / 2;
      int px[6] = {cx, cx + r * 87 / 100, cx + r * 87 / 100, cx, cx - r * 87 / 100, cx - r * 87 / 100};
      int py[6] = {y, y + r / 2, y + r * 3 / 2, y + s, y + r * 3 / 2, y + r / 2};
      for (int i = 0; i < 6; i++) c.line(px[i], py[i], px[(i + 1) % 6], py[(i + 1) % 6], ink, t);
      c.line(px[0], py[0] + t, px[4] + t, py[4], ink, t > 2 ? 2 : 1);
      c.line(px[0], py[0] + t, px[2] - t, py[2], ink, t > 2 ? 2 : 1);
      c.line(px[4] + t, py[4], px[2] - t, py[2], ink, t > 2 ? 2 : 1);
      break;
    }
    case Kind::Focus:
      c.circle(x + s / 2, y + s / 2, s / 2, ink, t);
      c.circle(x + s / 2, y + s / 2, s / 2 - P(22), ink, t);
      c.circle_fill(x + s / 2, y + s / 2, P(9), ink);
      break;
    case Kind::Brief: {
      int cx = x + s / 2, cy = y + s / 2;
      c.circle(cx, cy, P(22), ink, t);
      static const int dx[8] = {0, 71, 100, 71, 0, -71, -100, -71};
      static const int dy[8] = {-100, -71, 0, 71, 100, 71, 0, -71};
      for (int i = 0; i < 8; i++)
        c.line(cx + dx[i] * P(34) / 100, cy + dy[i] * P(34) / 100, cx + dx[i] * P(48) / 100,
               cy + dy[i] * P(48) / 100, ink, t);
      break;
    }
    case Kind::Doc:
      c.vline(x + P(12), y, s, ink, t);
      c.hline(x + P(12), y + s - t, P(76), ink, t);
      c.vline(x + P(88) - t, y + P(28), P(72), ink, t);
      c.hline(x + P(12), y, P(50), ink, t);
      c.line(x + P(62), y, x + P(88) - t, y + P(28), ink, t);
      c.hline(x + P(28), y + P(50), P(44), ink, t);
      c.hline(x + P(28), y + P(70), P(44), ink, t);
      break;
    default:
      c.circle_fill(x + s / 2, y + s / 2, s / 4, ink);
      break;
  }
}

void icon_battery(Canvas& c, int x, int y, int pct, bool charging, Ink ink) {
  c.round_rect(x, y, 23, 12, 2, ink, 2);
  c.fill(x + 23, y + 3, 3, 6, ink);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int w = 17 * pct / 100;
  if (pct > 0 && w < 2) w = 2;
  c.fill(x + 3, y + 3, w, 6, ink);
  if (charging) {
    c.line(x + 13, y + 1, x + 9, y + 6, Ink::White, 2);
    c.line(x + 9, y + 6, x + 14, y + 6, Ink::White, 2);
    c.line(x + 14, y + 6, x + 10, y + 11, Ink::White, 2);
  }
}

void icon_wifi(Canvas& c, int x, int y, bool ok, Ink ink) {
  int cx = x + 9, cy = y + 14;
  for (int r = 4; r <= 14; r += 5) {
    for (int dy = -r; dy <= 0; dy++)
      for (int dx = -r; dx <= r; dx++) {
        int d = dx * dx + dy * dy;
        if (d <= r * r && d > (r - 2) * (r - 2) && -dy * 10 >= (dx < 0 ? -dx : dx) * 8) c.px(cx + dx, cy + dy, ink);
      }
  }
  c.circle_fill(cx, cy - 1, 1, ink);
  if (!ok) c.line(x, y, x + 18, y + 14, ink, 2);
}

void icon_moon(Canvas& c, int x, int y, int s, Ink ink) {
  int r = s / 2;
  c.circle_fill(x + r, y + r, r, ink);
  c.circle_fill(x + r + r * 45 / 100, y + r - r * 35 / 100, r * 85 / 100, Ink::White);
}

}  // namespace cink
