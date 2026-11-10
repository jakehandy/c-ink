#include "cink/icons.h"

#include <string.h>

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
    case Kind::Chat: {
      // Slack's hash: four rounded bars in a pinwheel, each with a dot.
      int w = P(20) > 3 ? P(20) : 3, r = w / 2;
      c.round_fill(x + P(24), y, w, P(60), r, ink);           // top left, down
      c.round_fill(x + P(56), y + P(40), w, P(60), r, ink);   // bottom right, up
      c.round_fill(x, y + P(56), P(60), w, r, ink);           // bottom left, across
      c.round_fill(x + P(40), y + P(24), P(60), w, r, ink);   // top right, across
      c.round_fill(x + P(56), y, w, w, r, ink);
      c.round_fill(x + s - w, y + P(56), w, w, r, ink);
      c.round_fill(x + P(24), y + s - w, w, w, r, ink);
      c.round_fill(x, y + P(24), w, w, r, ink);
      break;
    }
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
    case Kind::Traffic:
      c.hline(x, y + s - t, s, ink, t);
      c.fill(x + P(6), y + P(58), P(20), P(42) - t, ink);
      c.fill(x + P(40), y + P(30), P(20), P(70) - t, ink);
      c.fill(x + P(74), y + P(4), P(20), P(96) - t, ink);
      break;
    case Kind::Db:
      // Three stacked discs, a database cylinder in outline.
      for (int i = 0; i < 3; i++) c.round_rect(x + P(6), y + i * P(33), P(88), P(34) + t, P(16), ink, t);
      break;
    case Kind::Commute: {
      // Map pin: a ring on top of a point.
      int cx = x + s / 2, cy = y + P(36), r = P(32);
      c.circle(cx, cy, r, ink, t + 1);
      c.circle_fill(cx, cy, P(11), ink);
      c.line(x + P(24), y + P(58), cx, y + P(98), ink, t + 1);
      c.line(x + P(76), y + P(58), cx, y + P(98), ink, t + 1);
      break;
    }
    case Kind::Wins: {
      // Five-point star.
      static const int sx[10] = {50, 61, 98, 68, 79, 50, 21, 32, 2, 39};
      static const int sy[10] = {0, 35, 36, 58, 96, 74, 96, 58, 36, 35};
      for (int i = 0; i < 10; i++)
        c.line(x + P(sx[i]), y + P(sy[i]), x + P(sx[(i + 1) % 10]), y + P(sy[(i + 1) % 10]), ink, t);
      break;
    }
    case Kind::News:
      // A folded newspaper: page outline, a headline bar, column lines.
      c.rect(x + P(8), y + P(10), P(84), P(80), ink, t);
      c.fill(x + P(20), y + P(22), P(60), P(14), ink);
      c.hline(x + P(20), y + P(48), P(26), ink, t);
      c.hline(x + P(20), y + P(62), P(26), ink, t);
      c.hline(x + P(54), y + P(48), P(26), ink, t);
      c.hline(x + P(54), y + P(62), P(26), ink, t);
      c.hline(x + P(20), y + P(76), P(60), ink, t);
      break;
    case Kind::Weather:
      wx_icon(c, Wx::Partly, x, y, s, ink);
      break;
    case Kind::Claude:
      // Fallback if the mark is missing at this size: an eight-point spark.
      for (int i = 0; i < 4; i++) {
        static const int dx[4] = {0, 71, 100, 71}, dy[4] = {-100, -71, 0, 71};
        c.line(x + s / 2 - dx[i] * s / 200, y + s / 2 - dy[i] * s / 200, x + s / 2 + dx[i] * s / 200,
               y + s / 2 + dy[i] * s / 200, ink, t);
      }
      break;
    case Kind::Cloud:
      wx_icon(c, Wx::Cloudy, x, y, s, ink);
      break;
    case Kind::Parcel:
      // A box seen from the front, with tape down the middle of the lid.
      c.line(x + P(50), y, x + s - 1, y + P(22), ink, t);
      c.line(x + P(50), y, x, y + P(22), ink, t);
      c.line(x, y + P(22), x + P(50), y + P(44), ink, t);
      c.line(x + s - 1, y + P(22), x + P(50), y + P(44), ink, t);
      c.vline(x, y + P(22), P(56), ink, t);
      c.vline(x + s - t, y + P(22), P(56), ink, t);
      c.vline(x + P(50) - t / 2, y + P(44), P(56), ink, t);
      c.line(x, y + P(78), x + P(50), y + s - 1, ink, t);
      c.line(x + s - 1, y + P(78), x + P(50), y + s - 1, ink, t);
      break;
    case Kind::Support:
    case Kind::Teams:
      // A speech bubble; Teams adds a second one behind it.
      c.round_rect(x, y + P(8), P(80), P(62), P(14), ink, t);
      c.line(x + P(18), y + P(70), x + P(14), y + P(92), ink, t);
      c.line(x + P(14), y + P(92), x + P(40), y + P(70), ink, t);
      if (k == Kind::Teams) {
        c.hline(x + P(80), y + P(30), P(20), ink, t);
        c.vline(x + s - t, y + P(30), P(48), ink, t);
        c.hline(x + P(60), y + P(78) - t, P(40), ink, t);
      } else {
        for (int i = 0; i < 3; i++) c.circle_fill(x + P(22) + i * P(18), y + P(39), P(6) > 1 ? P(6) : 1, ink);
      }
      break;
    default:
      c.circle_fill(x + s / 2, y + s / 2, s / 4, ink);
      break;
  }
}

void wx_icon(Canvas& c, Wx w, int x, int y, int s, Ink ink) {
  int t = s >= 40 ? 4 : s >= 28 ? 3 : 2;
  auto P = [&](int pct) { return pct * s / 100; };
  auto sun = [&](int cx, int cy, int r) {
    c.circle(cx, cy, r, ink, t);
    static const int dx[8] = {0, 71, 100, 71, 0, -71, -100, -71};
    static const int dy[8] = {-100, -71, 0, 71, 100, 71, 0, -71};
    for (int i = 0; i < 8; i++)
      c.line(cx + dx[i] * (r + P(10)) / 100, cy + dy[i] * (r + P(10)) / 100, cx + dx[i] * (r + P(22)) / 100,
             cy + dy[i] * (r + P(22)) / 100, ink, t);
  };
  // A cloud: three bumps on a flat base, outlined, with its inside cleared.
  auto cloud = [&](int cx, int cy, int cw) {
    int r1 = cw * 22 / 100, r2 = cw * 30 / 100, r3 = cw * 20 / 100;
    int base = cy + r2 / 2;
    int ax = cx - cw / 2 + r1, bx = cx - cw / 10, ex = cx + cw / 2 - r3;
    c.circle_fill(ax, base - r1, r1 + t, ink);
    c.circle_fill(bx, base - r2, r2 + t, ink);
    c.circle_fill(ex, base - r3, r3 + t, ink);
    c.fill(ax, base - r1, ex - ax, r1 + t, ink);
    c.circle_fill(ax, base - r1, r1, Ink::White);
    c.circle_fill(bx, base - r2, r2, Ink::White);
    c.circle_fill(ex, base - r3, r3, Ink::White);
    c.fill(ax, base - r1, ex - ax, r1, Ink::White);
  };
  switch (w) {
    case Wx::Clear: sun(x + s / 2, y + s / 2, P(24)); break;
    case Wx::Partly:
      sun(x + P(36), y + P(34), P(16));
      cloud(x + P(58), y + P(66), P(76));
      break;
    case Wx::Cloudy: cloud(x + s / 2, y + P(56), s); break;
    case Wx::Rain:
    case Wx::Storm:
      cloud(x + s / 2, y + P(44), s);
      if (w == Wx::Rain) {
        for (int i = 0; i < 3; i++) c.line(x + P(30 + i * 20), y + P(72), x + P(24 + i * 20), y + P(96), ink, t);
      } else {
        c.line(x + P(54), y + P(64), x + P(40), y + P(84), ink, t + 1);
        c.line(x + P(40), y + P(84), x + P(58), y + P(84), ink, t + 1);
        c.line(x + P(58), y + P(84), x + P(44), y + P(100), ink, t + 1);
      }
      break;
    case Wx::Snow:
      cloud(x + s / 2, y + P(44), s);
      for (int i = 0; i < 3; i++) c.circle_fill(x + P(28 + i * 22), y + P(84 + (i % 2) * 8), t + 1, ink);
      break;
    case Wx::Fog:
      for (int i = 0; i < 4; i++) c.hline(x + P(i % 2 ? 0 : 12), y + P(20 + i * 20), P(88), ink, t);
      break;
    default: break;
  }
}

// ------------------------------------------------------------- brand marks

namespace {
struct BrandBitmap {
  const char* id;
  uint8_t size;
  const uint8_t* bits;
};
#include "brands_data.inc"
}  // namespace

bool brand_icon(Canvas& c, const char* brand, int x, int y, int s, Ink ink) {
  if (!brand || !*brand) return false;
  for (const BrandBitmap& b : kBrandBitmaps) {
    if (b.size != s || strcmp(b.id, brand) != 0) continue;
    const int stride = (s + 7) / 8;
    for (int yy = 0; yy < s; yy++)
      for (int xx = 0; xx < s; xx++)
        if (b.bits[yy * stride + (xx >> 3)] & (0x80 >> (xx & 7))) c.px(x + xx, y + yy, ink);
    return true;
  }
  return false;
}

// The brand a kind's data usually comes from, when the card does not say.
static const char* kind_brand(Kind k) {
  switch (k) {
    case Kind::Event: return "gcal";
    case Kind::Review: return "github";
    case Kind::Mail: return "gmail";
    case Kind::Task: return "linear";
    case Kind::Ship: return "vercel";
    case Kind::Money: return "stripe";
    case Kind::Doc: return "gdrive";
    case Kind::Traffic: return "vercel";
    case Kind::Db: return "supabase";
    case Kind::Claude: return "claude";
    case Kind::Cloud: return "cloudflare";
    case Kind::Chat: return "slack";
    case Kind::Music: return "once";
    case Kind::Quest: return "tavern";
    case Kind::Support: return "intercom";
    case Kind::Teams: return "teams";
    default: return nullptr;
  }
}

void card_icon(Canvas& c, const Card& card, int x, int y, int s, Ink ink) {
  if (card.kind == Kind::Weather && card.wx != Wx::None) { wx_icon(c, card.wx, x, y, s, ink); return; }
  // "shop" is Shop by Shopify; anything without a mark falls back to the kind's line icon.
  const char* src = card.src[0] ? (strcmp(card.src, "shop") == 0 ? "shopify" : card.src) : kind_brand(card.kind);
  if (!brand_icon(c, src, x, y, s, ink)) icon(c, card.kind, x, y, s, ink);
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
