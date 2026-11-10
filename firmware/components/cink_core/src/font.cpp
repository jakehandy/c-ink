#include "cink/font.h"

namespace cink {

#include "fonts_data.inc"

const Glyph* find_glyph(const Font& f, uint32_t cp) {
  int lo = 0, hi = f.nglyphs - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t c = f.glyphs[mid].cp;
    if (c == cp) return &f.glyphs[mid];
    if (c < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

int kern_q(const Font& f, uint32_t a, uint32_t b) {
  if (!f.kerns || a > 0xFFFF || b > 0xFFFF) return 0;
  uint32_t key = (a << 16) | b;
  int lo = 0, hi = f.nkerns - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t k = f.kerns[mid].pair;
    if (k == key) return f.kerns[mid].dx;
    if (k < key) lo = mid + 1; else hi = mid - 1;
  }
  return 0;
}

static uint32_t fold(uint32_t cp) {
  switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: return '-';
    case '\n': case '\r': case '\t':
    case 0x00A0: case 0x2009: case 0x200A: case 0x202F: return ' ';
    case 0x200B: case 0xFE0F: case 0x200D: return 0;  // zero-width, dropped
    default: return cp;
  }
}

uint32_t next_cp(const char** sp) {
  for (;;) {
    const unsigned char* s = (const unsigned char*)*sp;
    if (!*s) return 0;
    uint32_t cp;
    int n;
    if (s[0] < 0x80) { cp = s[0]; n = 1; }
    else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) { cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); n = 2; }
    else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
      cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); n = 3;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
      cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); n = 4;
    } else { cp = '?'; n = 1; }
    *sp += n;
    cp = fold(cp);
    if (cp == 0) continue;
    if (cp >= 0x1F000) continue;  // emoji: skip instead of drawing boxes
    return cp;
  }
}

}  // namespace cink
