#pragma once
#include <stdint.h>

#include "cink/font.h"

namespace cink {

enum class Ink : uint8_t { White = 0, Black = 1, Accent = 2 };

// Two 1-bit planes in the UI's portrait orientation, 480 wide by 720 tall.
// The black plane is what a fast monochrome refresh pushes; the accent plane
// marks the few pixels that should come out in the accent color on a color
// refresh (and render black when the device is in mono-only mode).
//
// The panel itself scans 720x480 landscape. Panel drivers read the planes
// through native_byte(), which rotates on the fly, so no second frame buffer
// is needed.
class Canvas {
 public:
  static constexpr int W = 480;
  static constexpr int H = 720;
  static constexpr int STRIDE = W / 8;
  static constexpr int PLANE = STRIDE * H;
  static constexpr int NATIVE_W = 720;
  static constexpr int NATIVE_H = 480;

  // Byte bx (0..89) of native panel row ny (0..479) of `plane`, MSB first.
  // rotation 0: the badge's top edge is the panel's left edge; 1: its right.
  static uint8_t native_byte(const uint8_t* plane, int ny, int bx, int rotation);

  uint8_t black[PLANE];
  uint8_t accent[PLANE];

  void clear();
  void px(int x, int y, Ink ink);
  Ink get(int x, int y) const;
  bool has_accent() const;
  uint32_t hash() const;

  void fill(int x, int y, int w, int h, Ink ink);
  void hline(int x, int y, int w, Ink ink, int t = 1) { fill(x, y, w, t, ink); }
  void vline(int x, int y, int h, Ink ink, int t = 1) { fill(x, y, t, h, ink); }
  void rect(int x, int y, int w, int h, Ink ink, int t = 1);
  void round_fill(int x, int y, int w, int h, int r, Ink ink);
  void round_rect(int x, int y, int w, int h, int r, Ink ink, int t = 1);
  void circle_fill(int cx, int cy, int r, Ink ink);
  void circle(int cx, int cy, int r, Ink ink, int t = 1);
  void line(int x0, int y0, int x1, int y1, Ink ink, int t = 1);
  // Ordered-dither tint, level 1 (lightest) .. 3 (darkest). Reads as gray on mono e-paper.
  void tint(int x, int y, int w, int h, int level, Ink ink = Ink::Black);
  // Dotted horizontal rule, used as a quiet separator.
  void dots(int x, int y, int w, int gap, Ink ink);

  // Draws UTF-8 text with its baseline at y. tracking is extra quarter pixels
  // per glyph. With max_w > 0, text that does not fit is cut with an ellipsis.
  // Returns the drawn width in pixels.
  int text(const Font& f, int x, int y, const char* s, Ink ink, int tracking = 0, int max_w = 0);
  static int measure(const Font& f, const char* s, int tracking = 0);
  static int measure_n(const Font& f, const char* s, int nbytes, int tracking = 0);

  // Greedy word wrap into at most max_lines lines; the last line gets an
  // ellipsis when text remains. Returns the number of lines used. With
  // dry_run, nothing is drawn (for layout decisions).
  int wrap(const Font& f, int x, int y, int w, int line_h, int max_lines, const char* s, Ink ink,
           bool dry_run = false, int tracking = 0);

 private:
  void glyph(const Font& f, const Glyph& g, int x, int y, Ink ink);
};

}  // namespace cink
