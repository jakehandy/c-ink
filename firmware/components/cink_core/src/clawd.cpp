#include "cink/clawd.h"

namespace cink {

namespace {

// Poses come from tools/clawdgen.py: each is a 40 x 32 grid of half pixels
// (Clawd's 24 x 16 figure plus 8 free on either side and 16 above), four
// bits each: 0 nothing, 1 body, 2 black, 3 white, 4 shade, 5 gray, 6 light,
// 7 black drawn on the body.
#include "clawd_data.inc"

constexpr int GW = 40, GH = 32;
static_assert(sizeof kClawdFrames / sizeof kClawdFrames[0] == (size_t)Mood::Count, "one frame per mood");

const char* const kNames[(int)Mood::Count] = {
    "calm", "blink", "look-left", "look-right", "wink", "happy", "wave", "alert", "surprised", "puzzled",
    "sleepy", "busy", "thinking", "focused", "love", "sweat", "proud", "cool", "rainy", "cold",
    "reading", "coffee", "walking", "music", "typing", "celebrate", "parcel", "coin-hop", "crouch", "peek",
    "builder", "wizard", "detective", "skate", "flag", "lift", "float"};

}  // namespace

const char* mood_name(Mood m) { return (int)m < (int)Mood::Count ? kNames[(int)m] : "calm"; }

int clawd_headroom(Mood mood, int cell) {
  return kClawdHeadroom[(int)mood < (int)Mood::Count ? (int)mood : 0] * cell / 2;
}

void clawd(Canvas& cv, int x, int y, int cell, Mood mood, Ink body) {
  const uint8_t* f = kClawdFrames[(int)mood < (int)Mood::Count ? (int)mood : 0];
  // On a black-only panel the body is black, so what is drawn on it turns white.
  const bool mono = body == Ink::Black;
  // Half pixels: cell / 2, with any odd remainder spread so the body keeps its width.
  const int ox = x - 4 * cell, oy = y - 8 * cell;
  auto edge = [cell](int i) { return i * cell / 2; };
  for (int j = 0; j < GH; j++) {
    for (int i = 0; i < GW; i++) {
      int v = (f[j * (GW / 2) + i / 2] >> (i % 2 ? 0 : 4)) & 15;
      if (!v) continue;
      int x0 = ox + edge(i), y0 = oy + edge(j), w = ox + edge(i + 1) - x0, h = oy + edge(j + 1) - y0;
      // Tones are dithered a screen pixel at a time, in step with the sprite.
      Ink base = body, dot = body;
      int level = 0;  // 0 flat, 2 one pixel in eight, 3 every other pixel
      switch (v) {
        case 2: base = Ink::Black; break;
        case 3: base = Ink::White; break;
        case 4: dot = mono ? Ink::White : Ink::Black; level = 2; break;
        case 5: base = Ink::White; dot = Ink::Black; level = 3; break;
        case 6: base = Ink::White; level = 3; break;
        case 7: base = mono ? Ink::White : Ink::Black; break;
        default: break;
      }
      cv.fill(x0, y0, w, h, base);
      if (!level) continue;
      for (int yy = y0; yy < y0 + h; yy++)
        for (int xx = x0; xx < x0 + w; xx++) {
          int dx = xx - ox, dy = yy - oy;
          bool on = level == 3 ? ((dx + dy) & 1) == 0 : (dx & 1) == 0 && (dy & 1) == 0 && (((dx >> 1) + (dy >> 1)) & 1) == 0;
          if (on) cv.px(xx, yy, dot);
        }
    }
  }
}

}  // namespace cink
