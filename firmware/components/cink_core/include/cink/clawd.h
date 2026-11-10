#pragma once
#include "cink/canvas.h"

namespace cink {

// Clawd, the Claude Code mascot, as the badge's companion. Drawn from the
// same 18x5 block-pixel sprite as Claude Code's terminal banner, so it stays
// crisp at any scale. Cells are twice as tall as they are wide, like the
// terminal's half-block characters.
enum class Mood : uint8_t {
  Calm,     // default
  Happy,    // all clear, wins: claws up
  Alert,    // something urgent: "!"
  Puzzled,  // offline / never synced: "?"
  Sleepy,   // quiet hours: eyes shut, "z"
  Busy,     // a meeting is about to start: "..."
};

// Draws Clawd with its top-left at (x, y). cell = width of one sprite pixel
// in screen pixels (height is 2 * cell). The sprite is 18 x 5 cells, so the
// figure is 18*cell wide and 10*cell tall, plus 3 cells above for the mood mark.
void clawd(Canvas& cv, int x, int y, int cell, Mood mood, Ink body);
constexpr int clawd_w(int cell) { return 18 * cell; }
constexpr int clawd_h(int cell) { return 10 * cell; }

}  // namespace cink
