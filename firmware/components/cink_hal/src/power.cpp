#include "cink_hal/hal.h"

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "sdkconfig.h"

namespace hal {

#if CONFIG_CINK_BATT_ADC_GPIO >= 0
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;
static bool s_adc_ok;
#endif

void power_init() {
#if CONFIG_CINK_BATT_ADC_GPIO >= 0
  adc_unit_t unit;
  if (adc_oneshot_io_to_channel(CONFIG_CINK_BATT_ADC_GPIO, &unit, &s_chan) != ESP_OK) return;
  adc_oneshot_unit_init_cfg_t ucfg = {};
  ucfg.unit_id = unit;
  if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) return;
  adc_oneshot_chan_cfg_t ccfg = {};
  ccfg.atten = ADC_ATTEN_DB_12;
  ccfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  adc_oneshot_config_channel(s_adc, s_chan, &ccfg);
  adc_cali_curve_fitting_config_t cal = {};
  cal.unit_id = unit;
  cal.chan = s_chan;
  cal.atten = ADC_ATTEN_DB_12;
  cal.bitwidth = ADC_BITWIDTH_DEFAULT;
  s_adc_ok = adc_cali_create_scheme_curve_fitting(&cal, &s_cali) == ESP_OK;
#endif
#if CONFIG_CINK_PIN_CHARGING >= 0
  gpio_config_t c = {};
  c.pin_bit_mask = 1ULL << CONFIG_CINK_PIN_CHARGING;
  c.mode = GPIO_MODE_INPUT;
  c.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&c);
#endif
}

int battery_percent() {
#if CONFIG_CINK_BATT_ADC_GPIO >= 0
  if (!s_adc_ok) return -1;
  int sum = 0;
  for (int i = 0; i < 8; i++) {
    int raw = 0, mv = 0;
    adc_oneshot_read(s_adc, s_chan, &raw);
    adc_cali_raw_to_voltage(s_cali, raw, &mv);
    sum += mv;
  }
  int mv = sum / 8 * CONFIG_CINK_BATT_DIVIDER_X100 / 100;
  // Single-cell LiPo, resting voltage to charge: a coarse piecewise curve.
  static const int volts[] = {3300, 3600, 3700, 3750, 3800, 3900, 4000, 4100, 4200};
  static const int pct[] = {0, 8, 20, 35, 50, 65, 78, 90, 100};
  if (mv <= volts[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (mv <= volts[i]) return pct[i - 1] + (pct[i] - pct[i - 1]) * (mv - volts[i - 1]) / (volts[i] - volts[i - 1]);
  return 100;
#else
  return -1;
#endif
}

bool charging() {
#if CONFIG_CINK_PIN_CHARGING >= 0
  return gpio_get_level((gpio_num_t)CONFIG_CINK_PIN_CHARGING) == 0;
#else
  return usb_connected();
#endif
}

bool usb_connected() { return usb_serial_jtag_is_connected(); }

}  // namespace hal
