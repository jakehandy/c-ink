#include "cink_hal/hal.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

namespace hal {

static const int kPins[3] = {CONFIG_CINK_PIN_BTN_UP, CONFIG_CINK_PIN_BTN_DOWN, CONFIG_CINK_PIN_BTN_BACK};
static const uint8_t kBits[3] = {BTN_UP, BTN_DOWN, BTN_BACK};

void buttons_init() {
  for (int pin : kPins) {
    if (pin < 0) continue;
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << pin;
    c.mode = GPIO_MODE_INPUT;
#if CONFIG_CINK_BTN_ACTIVE_LOW
    c.pull_up_en = GPIO_PULLUP_ENABLE;
#else
    c.pull_down_en = GPIO_PULLDOWN_ENABLE;
#endif
    gpio_config(&c);
  }
}

uint8_t buttons_read() {
  uint8_t m = 0;
  for (int i = 0; i < 3; i++) {
    if (kPins[i] < 0) continue;
    int level = gpio_get_level((gpio_num_t)kPins[i]);
#if CONFIG_CINK_BTN_ACTIVE_LOW
    if (level == 0) m |= kBits[i];
#else
    if (level == 1) m |= kBits[i];
#endif
  }
  return m;
}

uint64_t buttons_wake_mask() {
  uint64_t m = 0;
  for (int pin : kPins)
    if (pin >= 0) m |= 1ULL << pin;
  return m;
}

bool buttons_wake_active_low() {
#if CONFIG_CINK_BTN_ACTIVE_LOW
  return true;
#else
  return false;
#endif
}

void buttons_prepare_sleep() {
  uint64_t mask = buttons_wake_mask();
  if (!mask) return;
  for (int pin : kPins) {
    if (pin < 0 || !rtc_gpio_is_valid_gpio((gpio_num_t)pin)) continue;
#if CONFIG_CINK_BTN_ACTIVE_LOW
    rtc_gpio_pullup_en((gpio_num_t)pin);
    rtc_gpio_pulldown_dis((gpio_num_t)pin);
#else
    rtc_gpio_pulldown_en((gpio_num_t)pin);
    rtc_gpio_pullup_dis((gpio_num_t)pin);
#endif
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(mask, buttons_wake_active_low() ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH);
}

uint8_t buttons_from_wake_mask(uint64_t pins) {
  uint8_t m = 0;
  for (int i = 0; i < 3; i++)
    if (kPins[i] >= 0 && (pins & (1ULL << kPins[i]))) m |= kBits[i];
  return m;
}

}  // namespace hal
