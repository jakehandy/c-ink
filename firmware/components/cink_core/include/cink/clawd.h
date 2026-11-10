#pragma once
#include "cink/canvas.h"

namespace cink {

// Clawd, the Claude Code mascot, as the badge's companion: the 12 x 8 pixel
// crab with solid black eyes, arms out at the sides and four legs, in poses
// drawn by tools/clawdgen.py. C-Ink adds a mark or a prop for what is on
// screen: hats, a newspaper, a flag, a skateboard.
enum class Mood : uint8_t {
  Calm,       // default
  Blink,      // eyes shut for a beat
  LookLeft,
  LookRight,
  Wink,
  Happy,      // all clear: arms up
  Wave,       // hello / Claude Code wants you: one arm up
  Alert,      // something urgent: "!"
  Surprised,  // a meeting in five minutes: a hop with arms up
  Puzzled,    // offline / never synced: "?"
  Sleepy,     // quiet hours: eyes shut, crouched, "z z"
  Busy,       // a meeting soon: tapping a foot, "..."
  Thinking,   // waiting on a sync: thought bubbles
  Focused,    // focus block: turned away, heads down
  Love,       // a heart
  Sweat,      // too many fires: a sweat drop
  Proud,      // wins: arms up, sparkle
  Cool,       // sunny weather: sunglasses
  Rainy,      // rain: drops overhead
  Cold,       // snow: a sneeze
  Reading,    // news: peeking over a newspaper
  Coffee,     // morning brief: a mug with steam
  Walking,    // commute: side on, mid-step, speed lines
  Music,      // releases: a note, tapping along
  Typing,     // Claude Code is working: a laptop
  Celebrate,  // a great day: arms up, sparkles either side
  Boop,       // a delivery arrived: eyes shut, a little poof
  CoinHop,    // revenue up: hopping with a shadow below
  Crouch,     // ready to go: crouched, looking ahead
  Peek,       // something new: turned halfway, peeking
  Builder,    // deploys and databases: hard hat and wrench
  Wizard,     // Claude Code at work: pointed hat and a wand
  Detective,  // pull requests to review: fedora and a magnifier
  Skate,      // commute: on a skateboard
  Flag,       // shipped / the week's wins: a checkered flag
  Lift,       // heads-down focus: dumbbells and a headband
  Float,      // idle: hovering
  Count
};

// Draws Clawd with its top-left at (x, y). cell = one sprite pixel in screen
// pixels (even values keep the half pixels crisp). The figure is 12*cell wide
// and 8*cell tall (feet on the bottom edge); marks and props reach up to 4
// cells above and to either side.
void clawd(Canvas& cv, int x, int y, int cell, Mood mood, Ink body);
constexpr int clawd_w(int cell) { return 12 * cell; }
constexpr int clawd_h(int cell) { return 8 * cell; }
const char* mood_name(Mood m);

}  // namespace cink
