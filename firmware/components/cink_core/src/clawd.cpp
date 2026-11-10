#include "cink/clawd.h"

namespace cink {

namespace {

// Poses come from tools/clawdgen.py: each is a 40 x 24 grid of half pixels
// (Clawd's 12 x 8 body plus 4 pixels free on either side and above), two
// bits each: 0 nothing, 1 body, 2 black, 3 white.
#include "clawd_data.inc"

constexpr int GW = 40, GH = 24;
static_assert(sizeof kClawdFrames / sizeof kClawdFrames[0] == (size_t)Mood::Count, "one frame per mood");

const char* const kNames[(int)Mood::Count] = {
    "calm", "blink", "look-left", "look-right", "wink", "happy", "wave", "alert", "surprised", "puzzled",
    "sleepy", "busy", "thinking", "focused", "love", "sweat", "proud", "cool", "rainy", "cold",
    "reading", "coffee", "walking", "music", "typing", "celebrate", "boop", "coin-hop", "crouch", "peek",
    "builder", "wizard", "detective", "skate", "flag", "lift", "float"};

}  // namespace

const char* mood_name(Mood m) { return (int)m < (int)Mood::Count ? kNames[(int)m] : "calm"; }

void clawd(Canvas& cv, int x, int y, int cell, Mood mood, Ink body) {
  const uint8_t* f = kClawdFrames[(int)mood < (int)Mood::Count ? (int)mood : 0];
  // Half pixels: cell / 2, with any odd remainder spread so the body keeps its width.
  const int ox = x - 4 * cell, oy = y - 4 * cell;
  auto edge = [cell](int i) { return i * cell / 2; };
  for (int j = 0; j < GH; j++) {
    for (int i = 0; i < GW; i++) {
      int v = (f[j * (GW / 4) + i / 4] >> (6 - 2 * (i % 4))) & 3;
      if (!v) continue;
      Ink ink = v == 1 ? body : v == 2 ? Ink::Black : Ink::White;
      int x0 = ox + edge(i), y0 = oy + edge(j);
      cv.fill(x0, y0, ox + edge(i + 1) - x0, oy + edge(j + 1) - y0, ink);
    }
  }
}

}  // namespace cink
