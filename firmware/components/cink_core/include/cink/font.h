#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cink {

// One glyph of a 1-bit font. Bits are a packed row-major stream starting at
// `off` in Font::bits. xo/yo place the bitmap's top-left relative to the pen
// position on the baseline. Advances are in quarter pixels.
struct Glyph {
  uint16_t cp;
  uint8_t w, h;
  int8_t xo, yo;
  uint16_t adv;
  uint32_t off;
};

struct Kern {
  uint32_t pair;  // (left << 16) | right
  int8_t dx;      // quarter pixels
};

struct Font {
  const uint8_t* bits;
  const Glyph* glyphs;
  const Kern* kerns;
  uint16_t nglyphs, nkerns;
  uint8_t ascent, descent, size;
};

extern const Font font_caption;  // Inter SemiBold 15, eyebrows and labels
extern const Font font_small;    // Inter Medium 18
extern const Font font_body;     // Inter Regular 22
extern const Font font_body_b;   // Inter SemiBold 22
extern const Font font_sub;      // Inter Regular 26
extern const Font font_tile;     // Inter SemiBold 20
extern const Font font_title_s;  // Inter Display Bold 36
extern const Font font_title;    // Inter Display Bold 46
extern const Font font_big;      // Inter Display Bold 72
extern const Font font_huge;     // Inter Display Bold 132, digits only

const Glyph* find_glyph(const Font& f, uint32_t cp);
int kern_q(const Font& f, uint32_t left, uint32_t right);

// Decodes one UTF-8 code point and advances *s. Returns 0 at end of string.
// Typographic punctuation the fonts do not carry is folded to ASCII.
uint32_t next_cp(const char** s);

}  // namespace cink
