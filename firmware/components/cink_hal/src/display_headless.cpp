// Headless display: logs a fingerprint of each frame. Lets everything above
// the panel run on a bare ESP32-S3 dev board or on a TICKEY before the panel
// driver is public. The USB "frame" op reads the app's canvas directly.
#include <stddef.h>

#include "cink_hal/hal.h"
#include "esp_log.h"

namespace hal {

static const char* TAG = "display";
static constexpr size_t kPlane = 720 / 8 * 480;

bool display_init() {
  ESP_LOGI(TAG, "headless backend (no panel driver linked)");
  return true;
}

void display_present(const uint8_t* black, const uint8_t* accent, Refresh mode) {
  (void)accent;
  uint32_t ink = 0;
  for (size_t i = 0; i < kPlane; i++) ink += __builtin_popcount(black[i]);
  ESP_LOGI(TAG, "present %s refresh, %lu black px", mode == Refresh::Full ? "full" : "fast", (unsigned long)ink);
}

void display_sleep() {}

}  // namespace hal
