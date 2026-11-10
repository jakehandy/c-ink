#include "cink/canvas.h"

#include <string.h>

namespace cink {

void Canvas::clear() {
  memset(black, 0, sizeof black);
  memset(accent, 0, sizeof accent);
}

void Canvas::px(int x, int y, Ink ink) {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
  int i = y * STRIDE + (x >> 3);
  uint8_t m = 0x80 >> (x & 7);
  switch (ink) {
    case Ink::White: black[i] &= ~m; accent[i] &= ~m; break;
    case Ink::Black: black[i] |= m; accent[i] &= ~m; break;
    case Ink::Accent: black[i] |= m; accent[i] |= m; break;
  }
}

Ink Canvas::get(int x, int y) const {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return Ink::White;
  int i = y * STRIDE + (x >> 3);
  uint8_t m = 0x80 >> (x & 7);
  if (accent[i] & m) return Ink::Accent;
  return (black[i] & m) ? Ink::Black : Ink::White;
}

bool Canvas::has_accent() const {
  for (int i = 0; i < PLANE; i++) if (accent[i]) return true;
  return false;
}

uint32_t Canvas::hash() const {
  uint32_t h = 2166136261u;  // FNV-1a over both planes
  for (int i = 0; i < PLANE; i++) { h ^= black[i]; h *= 16777619u; }
  for (int i = 0; i < PLANE; i++) { h ^= accent[i]; h *= 16777619u; }
  return h;
}

void Canvas::fill(int x, int y, int w, int h, Ink ink) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) px(xx, yy, ink);
}

void Canvas::rect(int x, int y, int w, int h, Ink ink, int t) {
  fill(x, y, w, t, ink);
  fill(x, y + h - t, w, t, ink);
  fill(x, y, t, h, ink);
  fill(x + w - t, y, t, h, ink);
}

static int isqrt(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

void Canvas::round_fill(int x, int y, int w, int h, int r, Ink ink) {
  if (r * 2 > h) r = h / 2;
  if (r * 2 > w) r = w / 2;
  for (int yy = 0; yy < h; yy++) {
    int inset = 0;
    int dy = -1;
    if (yy < r) dy = r - yy;
    else if (yy >= h - r) dy = yy - (h - r - 1);
    if (dy >= 0) inset = r - isqrt(r * r - (dy - 1) * (dy - 1) > 0 ? r * r - (dy - 1) * (dy - 1) : 0);
    fill(x + inset, y + yy, w - 2 * inset, 1, ink);
  }
}

void Canvas::round_rect(int x, int y, int w, int h, int r, Ink ink, int t) {
  // Outline as the difference of two filled shapes, row by row.
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) {
      auto inside = [&](int ax, int ay, int aw, int ah, int ar) {
        if (aw <= 0 || ah <= 0) return false;
        if (xx < ax || yy < ay || xx >= ax + aw || yy >= ay + ah) return false;
        int cx = xx < ax + ar ? ax + ar : (xx >= ax + aw - ar ? ax + aw - ar - 1 : xx);
        int cy = yy < ay + ar ? ay + ar : (yy >= ay + ah - ar ? ay + ah - ar - 1 : yy);
        int dx = xx - cx, dy = yy - cy;
        return dx * dx + dy * dy <= ar * ar;
      };
      int ri = r - t < 0 ? 0 : r - t;
      if (inside(x, y, w, h, r) && !inside(x + t, y + t, w - 2 * t, h - 2 * t, ri)) px(xx, yy, ink);
    }
  }
}

void Canvas::circle_fill(int cx, int cy, int r, Ink ink) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx * dx + dy * dy <= r * r + r / 2) px(cx + dx, cy + dy, ink);
}

void Canvas::circle(int cx, int cy, int r, Ink ink, int t) {
  int ro = r * r + r / 2, ri = (r - t) * (r - t) + (r - t) / 2;
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++) {
      int d = dx * dx + dy * dy;
      if (d <= ro && d > ri) px(cx + dx, cy + dy, ink);
    }
}

void Canvas::line(int x0, int y0, int x1, int y1, Ink ink, int t) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int h = t / 2;
  for (;;) {
    if (t <= 1) px(x0, y0, ink); else fill(x0 - h, y0 - h, t, t, ink);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas::tint(int x, int y, int w, int h, int level, Ink ink) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) {
      bool on;
      switch (level) {
        case 1: on = ((xx & 3) == 0) && ((yy & 3) == 0); break;           // 1/16
        case 2: on = ((xx + yy) & 1) == 0 && (yy & 1) == 0; break;        // 1/4
        default: on = ((xx + yy) & 1) == 0; break;                         // 1/2
      }
      if (on) px(xx, yy, ink);
    }
}

void Canvas::dots(int x, int y, int w, int gap, Ink ink) {
  for (int xx = x; xx < x + w; xx += gap) px(xx, y, ink);
}

void Canvas::glyph(const Font& f, const Glyph& g, int x, int y, Ink ink) {
  const uint8_t* b = f.bits + g.off;
  uint32_t bit = 0;
  int ox = x + g.xo, oy = y + g.yo;
  for (int r = 0; r < g.h; r++)
    for (int c = 0; c < g.w; c++, bit++)
      if (b[bit >> 3] & (0x80 >> (bit & 7))) px(ox + c, oy + r, ink);
}

static const Glyph* glyph_or_q(const Font& f, uint32_t cp) {
  const Glyph* g = find_glyph(f, cp);
  return g ? g : find_glyph(f, '?');
}

int Canvas::measure_n(const Font& f, const char* s, int nbytes, int tracking) {
  const char* end = nbytes < 0 ? nullptr : s + nbytes;
  int pen = 0;
  uint32_t prev = 0;
  while (!end || s < end) {
    uint32_t cp = next_cp(&s);
    if (!cp) break;
    const Glyph* g = glyph_or_q(f, cp);
    if (!g) continue;
    if (prev) pen += kern_q(f, prev, cp) + tracking;
    pen += g->adv;
    prev = cp;
  }
  return (pen + 2) / 4;
}

int Canvas::measure(const Font& f, const char* s, int tracking) { return measure_n(f, s, -1, tracking); }

int Canvas::text(const Font& f, int x, int y, const char* s, Ink ink, int tracking, int max_w) {
  if (!s) return 0;
  int limit_bytes = -1;
  bool ellipsis = false;
  if (max_w > 0 && measure(f, s, tracking) > max_w) {
    int ew = measure(f, "\xE2\x80\xA6", tracking);
    const char* p = s;
    int best = 0;
    for (;;) {
      const char* q = p;
      if (!next_cp(&q)) break;
      if (measure_n(f, s, (int)(q - s), tracking) + ew > max_w) break;
      p = q;
      best = (int)(p - s);
    }
    // Do not leave a dangling space or separator before the ellipsis.
    while (best > 0 && (s[best - 1] == ' ' || s[best - 1] == ',' || s[best - 1] == '.')) best--;
    limit_bytes = best;
    ellipsis = true;
  }
  int pen = x * 4;
  uint32_t prev = 0;
  const char* p = s;
  while (limit_bytes < 0 || p < s + limit_bytes) {
    uint32_t cp = next_cp(&p);
    if (!cp) break;
    const Glyph* g = glyph_or_q(f, cp);
    if (!g) continue;
    if (prev) pen += kern_q(f, prev, cp) + tracking;
    glyph(f, *g, (pen + 2) / 4, y, ink);
    pen += g->adv;
    prev = cp;
  }
  if (ellipsis) {
    const Glyph* g = find_glyph(f, 0x2026);
    if (g) {
      if (prev) pen += tracking;
      glyph(f, *g, (pen + 2) / 4, y, ink);
      pen += g->adv;
    }
  }
  return (pen + 2) / 4 - x;
}

int Canvas::wrap(const Font& f, int x, int y, int w, int line_h, int max_lines, const char* s, Ink ink,
                 bool dry_run, int tracking) {
  if (!s || !*s || max_lines <= 0) return 0;
  int lines = 0;
  const char* p = s;
  char buf[512];
  while (*p && lines < max_lines) {
    while (*p == ' ') p++;
    if (!*p) break;
    // Find the longest prefix that fits, breaking at spaces when possible.
    const char* best = nullptr;
    const char* q = p;
    const char* last_space = nullptr;
    while (*q && *q != '\n') {
      const char* nq = q;
      next_cp(&nq);
      if (measure_n(f, p, (int)(nq - p), tracking) > w) break;
      q = nq;
      if (*q == ' ' || *q == 0 || *q == '\n') last_space = q;
    }
    bool rest = *q && *q != '\n';
    if (!rest) best = q;
    else if (last_space && last_space > p) best = last_space;
    else best = q > p ? q : p + 1;  // a single word wider than the line: hard break
    int n = (int)(best - p);
    if (n >= (int)sizeof buf) n = sizeof buf - 1;
    memcpy(buf, p, n);
    buf[n] = 0;
    lines++;
    const char* after = best;
    while (*after == ' ') after++;
    if (*after == '\n') after++;
    bool more = *after != 0;
    if (!dry_run) {
      if (lines == max_lines && more) {
        // Last allowed line: pack in as much of the remainder as fits.
        text(f, x, y, p, ink, tracking, w);
      } else {
        text(f, x, y, buf, ink, tracking);
      }
    }
    y += line_h;
    p = after;
  }
  return lines;
}

}  // namespace cink
