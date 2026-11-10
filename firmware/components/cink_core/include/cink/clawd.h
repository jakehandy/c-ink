#pragma once
#include "cink/canvas.h"

namespace cink {

// Clawd, the Claude Code mascot, as the badge's companion. Drawn from the
// same 18x5 block-pixel sprite as Claude Code's terminal banner, so it stays
// crisp at any scale. Cells are twice as tall as they are wide, like the
// terminal's half-block characters. Each mood is a pose: eyes, claws, legs,
// and an optional mark or prop.
enum class Mood : uint8_t {
  Calm,       // default
  Blink,      // eyes shut for a beat
  LookLeft,
  LookRight,
  Wink,
  Happy,      // all clear: claws up, smiling eyes
  Wave,       // hello / Claude Code wants you: one claw up
  Alert,      // something urgent: wide eyes, "!"
  Surprised,  // a meeting in five minutes: wide eyes, claws up
  Puzzled,    // offline / never synced: "?"
  Sleepy,     // quiet hours: eyes shut, "z z"
  Busy,       // a meeting soon: "..."
  Thinking,   // waiting on a sync: thought bubbles
  Focused,    // focus block: lidded eyes, claws down at the keys
  Love,       // a heart
  Sweat,      // too many fires: a sweat drop
  Proud,      // wins, releases: claws up, sparkle
  Cool,       // sunny weather: sunglasses
  Rainy,      // rain: drops overhead
  Cold,       // cold or snow: shivering
  Reading,    // news: holding a paper
  Coffee,     // morning brief: a mug with steam
  Walking,    // commute: stepping, speed lines
  Music,      // releases: a note
  Typing,     // Claude Code is working: claws down, a laptop
  Count
};

// Draws Clawd with its top-left at (x, y). cell = width of one sprite pixel
// in screen pixels (height is 2 * cell). The sprite is 18 x 5 cells, so the
// figure is 18*cell wide and 10*cell tall, plus 3 cells above for the mood mark.
void clawd(Canvas& cv, int x, int y, int cell, Mood mood, Ink body);
constexpr int clawd_w(int cell) { return 18 * cell; }
constexpr int clawd_h(int cell) { return 10 * cell; }
const char* mood_name(Mood m);

}  // namespace cink
