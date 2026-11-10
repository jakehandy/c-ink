// Headless display: keeps the last frame for the USB frame dump and logs a
// fingerprint. Lets everything above the panel run on a bare ESP32-S3 dev
// board or on a TICKEY before the panel driver is public.
#include <string.h>

#include "cink_hal/hal.h"
#include "esp_log.h"

namespace hal {

static const char* TAG = "display";
static uint8_t s_black[720 / 8 * 480];
static uint8_t s_accent[720 / 8 * 480];

bool display_init() {
  ESP_LOGI(TAG, "headless backend (no panel driver linked)");
  return true;
}

void display_present(const uint8_t* black, const uint8_t* accent, Refresh mode) {
  memcpy(s_black, black, sizeof s_black);
  memcpy(s_accent, accent, sizeof s_accent);
  uint32_t ink = 0;
  for (size_t i = 0; i < sizeof s_black; i++) ink += __builtin_popcount(s_black[i]);
  ESP_LOGI(TAG, "present %s refresh, %lu black px", mode == Refresh::Full ? "full" : "fast", (unsigned long)ink);
}

void display_sleep() {}
const uint8_t* display_last_black() { return s_black; }
const uint8_t* display_last_accent() { return s_accent; }

}  // namespace hal
