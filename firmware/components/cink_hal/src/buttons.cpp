#include "cink_hal/board.h"
#include "cink_hal/hal.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"

namespace hal {

static const uint8_t kBits[3] = {BTN_UP, BTN_DOWN, BTN_BACK};

static int pin_of(int i) { return board().btn[i]; }

static void config_input(int pin, bool active_low) {
  gpio_config_t c = {};
  c.pin_bit_mask = 1ULL << pin;
  c.mode = GPIO_MODE_INPUT;
  c.pull_up_en = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  c.pull_down_en = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
  gpio_config(&c);
}

void buttons_init() {
  for (int i = 0; i < 3; i++)
    if (pin_of(i) >= 0) config_input(pin_of(i), board().btn_active_low);
}

uint8_t buttons_read() {
  uint8_t m = 0;
  const int pressed = board().btn_active_low ? 0 : 1;
  for (int i = 0; i < 3; i++)
    if (pin_of(i) >= 0 && gpio_get_level((gpio_num_t)pin_of(i)) == pressed) m |= kBits[i];
  return m;
}

uint64_t buttons_wake_mask() {
  uint64_t m = 0;
  for (int i = 0; i < 3; i++)
    if (pin_of(i) >= 0 && rtc_gpio_is_valid_gpio((gpio_num_t)pin_of(i))) m |= 1ULL << pin_of(i);
  return m;
}

bool buttons_wake_active_low() { return board().btn_active_low; }

void buttons_prepare_sleep() {
  uint64_t mask = buttons_wake_mask();
  if (!mask) return;
  for (int i = 0; i < 3; i++) {
    gpio_num_t pin = (gpio_num_t)pin_of(i);
    if (pin_of(i) < 0 || !rtc_gpio_is_valid_gpio(pin)) continue;
    if (board().btn_active_low) {
      rtc_gpio_pullup_en(pin);
      rtc_gpio_pulldown_dis(pin);
    } else {
      rtc_gpio_pulldown_en(pin);
      rtc_gpio_pullup_dis(pin);
    }
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(mask, buttons_wake_active_low() ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH);
}

uint8_t buttons_from_wake_mask(uint64_t pins) {
  uint8_t m = 0;
  for (int i = 0; i < 3; i++)
    if (pin_of(i) >= 0 && (pins & (1ULL << pin_of(i)))) m |= kBits[i];
  return m;
}

// ------------------------------------------------------------ pin learning

static uint64_t s_learn_mask, s_learn_prev;

// Pins never touched while learning: USB D-/D+ (19, 20, our link to the
// host), SPI flash and octal PSRAM (26-37), the UART0 console (43, 44) and
// anything the board profile already drives for the panel.
static bool learnable(int pin) {
  if (pin == 19 || pin == 20 || (pin >= 22 && pin <= 37) || pin == 43 || pin == 44 || pin > 48) return false;
  const Board& b = board();
  const int8_t used[] = {b.sck, b.mosi, b.cs, b.dc, b.rst, b.busy, b.pwr};
  for (int8_t u : used)
    if (u == pin) return false;
  return true;
}

static uint64_t sample() {
  uint64_t v = 0;
  for (int pin = 0; pin <= 48; pin++)
    if ((s_learn_mask >> pin & 1) && gpio_get_level((gpio_num_t)pin)) v |= 1ULL << pin;
  return v;
}

int learn_begin(bool active_low) {
  s_learn_mask = 0;
  int n = 0;
  for (int pin = 0; pin <= 48; pin++) {
    if (!learnable(pin)) continue;
    config_input(pin, active_low);
    s_learn_mask |= 1ULL << pin;
    n++;
  }
  s_learn_prev = sample();
  return n;
}

int learn_poll(int* level) {
  if (!s_learn_mask) return -1;
  uint64_t now = sample();
  uint64_t diff = now ^ s_learn_prev;
  if (!diff) return -1;
  int pin = __builtin_ctzll(diff);
  s_learn_prev ^= 1ULL << pin;
  if (level) *level = (int)(now >> pin & 1);
  return pin;
}

void learn_end() {
  for (int pin = 0; pin <= 48; pin++)
    if (s_learn_mask >> pin & 1) gpio_reset_pin((gpio_num_t)pin);
  s_learn_mask = 0;
  buttons_init();
}

}  // namespace hal
