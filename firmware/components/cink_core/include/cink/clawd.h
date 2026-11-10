#pragma once
#include "cink/canvas.h"

namespace cink {

// Clawd, the Claude Code mascot, as the badge's companion: the pixel crab
// with solid black eyes, arms out at the sides and four legs. Every pose is
// drawn by tools/clawdgen.py on one grid, the hats, tools and action poses
// traced from Anthropic's own Clawd pictures. C-Ink picks the pose for what
// is on screen.
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
  Cold,       // snow: eyes shut, shivering
  Reading,    // news: peeking over a newspaper
  Coffee,     // morning brief: a mug with steam
  Walking,    // commute on foot: three-quarter view, mid-stride, speed lines
  Music,      // releases: a note, tapping along
  Typing,     // Claude Code is working: a laptop
  Celebrate,  // a great week: a jump under confetti
  Parcel,     // a delivery: pushing a box on a line
  CoinHop,    // revenue up: hopping with a shadow below
  Crouch,     // ready to go: squatting, looking ahead
  Peek,       // something new: side on
  Builder,    // deploys and databases: hard hat and wrench
  Wizard,     // Claude Code at work: a starry pointed hat
  Detective,  // pull requests to review: fedora and a magnifier
  Skate,      // commute: three-quarter view on a skateboard
  Flag,       // shipped / the week's wins: a checkered flag
  Lift,       // heads-down focus: pressing a dumbbell, headband on
  Float,      // idle: hovering, legs tucked
  Count
};

// Draws Clawd with the figure's top-left at (x, y). cell = one Clawd pixel
// in screen pixels (even values keep the half pixels crisp). The figure is
// 12*cell wide and 8*cell tall (feet on the bottom edge); props reach up to
// 4 cells to either side and 8 above. With body = Ink::Black (a black-only
// panel) the eyes and anything else drawn on the body come out white.
void clawd(Canvas& cv, int x, int y, int cell, Mood mood, Ink body);
constexpr int clawd_w(int cell) { return 12 * cell; }
constexpr int clawd_h(int cell) { return 8 * cell; }
// Screen pixels this pose uses above the figure (hats, flags, marks).
int clawd_headroom(Mood mood, int cell);
const char* mood_name(Mood m);

}  // namespace cink
