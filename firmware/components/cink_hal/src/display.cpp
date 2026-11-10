// Display dispatcher. Picks the panel backend at boot from the board profile:
//
//   1. An SPI panel profile in NVS (sent by C-Ink Settings, usually built by
//      tools/tickey-probe from the stock firmware): the scripted driver in
//      panel_spi.cpp.
//   2. CONFIG_CINK_DISPLAY_TICKEY: the vendor BSP's tickey_panel_* API, once
//      ENILINX publishes it.
//   3. Otherwise headless: frames are logged and served over USB, so the
//      whole stack runs before the panel does.
#include <stddef.h>

#include "cink/canvas.h"
#include "cink_hal/board.h"
#include "cink_hal/hal.h"
#include "esp_log.h"
#include "panel_spi.h"
#include "sdkconfig.h"

#if CONFIG_CINK_DISPLAY_TICKEY
extern "C" {
int tickey_panel_init(void);
void tickey_panel_draw(const uint8_t* black, const uint8_t* color, int full, int rotation);
void tickey_panel_sleep(void);
}
#endif

namespace hal {

static const char* TAG = "display";
enum class Backend : uint8_t { Headless, Spi, Vendor };
static Backend s_backend = Backend::Headless;
static PanelReport s_headless;

bool display_init() {
  const Board& b = board();
  if (b.panel == PanelType::Spi) {
    if (panel_spi_init(b)) {
      s_backend = Backend::Spi;
      return true;
    }
    ESP_LOGE(TAG, "SPI panel profile did not start; falling back");
  }
#if CONFIG_CINK_DISPLAY_TICKEY
  if (tickey_panel_init() == 0) {
    s_backend = Backend::Vendor;
    return true;
  }
#endif
  s_backend = Backend::Headless;
  ESP_LOGI(TAG, "headless backend (no panel profile or driver)");
  return true;
}

void display_present(const uint8_t* black, const uint8_t* accent, Refresh mode, int rotation) {
  switch (s_backend) {
    case Backend::Spi: panel_spi_present(black, accent, mode, rotation); return;
#if CONFIG_CINK_DISPLAY_TICKEY
    case Backend::Vendor: tickey_panel_draw(black, accent, mode == Refresh::Full, rotation); return;
#endif
    default: break;
  }
  uint32_t ink = 0;
  for (size_t i = 0; i < (size_t)cink::Canvas::PLANE; i++) ink += __builtin_popcount(black[i]);
  s_headless.frames++;
  ESP_LOGI(TAG, "present %s refresh, %lu black px", mode == Refresh::Full ? "full" : "fast", (unsigned long)ink);
}

void display_sleep() {
  switch (s_backend) {
    case Backend::Spi: panel_spi_sleep(); break;
#if CONFIG_CINK_DISPLAY_TICKEY
    case Backend::Vendor: tickey_panel_sleep(); break;
#endif
    default: break;
  }
}

const char* display_backend() {
  switch (s_backend) {
    case Backend::Spi: return "spi";
    case Backend::Vendor: return "vendor";
    default: return "headless";
  }
}

PanelReport display_report() { return s_backend == Backend::Spi ? panel_spi_report() : s_headless; }

}  // namespace hal
