// TICKEY panel backend. ENILINX ships the Modos-based panel driver as part of
// its open firmware; once that is public, this file adapts it. The expected
// shape (see docs/hardware.md) is a tiny C API from the vendor BSP:
//
//   int  tickey_panel_init(void);
//   void tickey_panel_draw(const uint8_t* black, const uint8_t* color, int full);
//   void tickey_panel_sleep(void);
//
// where `color` marks pixels to show in the accent color on a full refresh.
#include "cink_hal/hal.h"

extern "C" {
int tickey_panel_init(void);
void tickey_panel_draw(const uint8_t* black, const uint8_t* color, int full);
void tickey_panel_sleep(void);
}

namespace hal {

bool display_init() { return tickey_panel_init() == 0; }

void display_present(const uint8_t* black, const uint8_t* accent, Refresh mode) {
  tickey_panel_draw(black, accent, mode == Refresh::Full);
}

void display_sleep() { tickey_panel_sleep(); }

}  // namespace hal
