// Scripted SPI e-paper driver. The controller specifics (init tables, LUT
// selection, RAM commands, refresh triggers) live in the board profile as
// bytecode (see board.h), so supporting a new panel controller means sending
// a new profile, not flashing new firmware.
//
// Frames are streamed row by row in the controller's native scan order,
// rotated and mirrored on the fly from the portrait canvas, so the driver
// needs one row buffer and no second frame.
#include "panel_spi.h"

#include <string.h>

#include "cink/canvas.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace hal {

static const char* TAG = "panel";
using cink::Canvas;

static const Board* s_b;
static spi_device_handle_t s_dev;
static bool s_awake;
static PanelReport s_rep;
// One native row: 720 px as 4 bpp is the largest case.
static uint8_t s_row[Canvas::NATIVE_W / 2] __attribute__((aligned(4)));

static void tx(const uint8_t* p, size_t n, bool data) {
  if (!n) return;
  gpio_set_level((gpio_num_t)s_b->dc, data ? 1 : 0);
  spi_transaction_t t = {};
  t.length = n * 8;
  t.tx_buffer = p;
  spi_device_polling_transmit(s_dev, &t);
}

static bool busy() { return gpio_get_level((gpio_num_t)s_b->busy) == (s_b->busy_high ? 1 : 0); }

static void wait_busy(int secs) {
  int64_t t0 = esp_timer_get_time();
  int64_t limit = (int64_t)(secs ? secs : 30) * 1000000;
  // Controllers take a few microseconds to raise BUSY after a command.
  esp_rom_delay_us(200);
  bool seen = false;
  while (busy()) {
    seen = true;
    if (esp_timer_get_time() - t0 > limit) {
      s_rep.timeouts++;
      ESP_LOGW(TAG, "BUSY still set after %d s", secs);
      break;
    }
    vTaskDelay(1);
  }
  if (seen) s_rep.busy_seen = true;
  s_rep.busy_ms += (uint32_t)((esp_timer_get_time() - t0) / 1000);
}

// Native panel pixel (nx, ny) -> logical canvas pixel.
static inline void to_logical(int nx, int ny, int rot, int& lx, int& ly) {
  if (s_b->native_portrait) {
    if (s_b->mirror) nx = Canvas::W - 1 - nx;
    if (rot) { lx = Canvas::W - 1 - nx; ly = Canvas::H - 1 - ny; }
    else { lx = nx; ly = ny; }
  } else {
    if (s_b->mirror) nx = Canvas::NATIVE_W - 1 - nx;
    if (rot) { lx = Canvas::NATIVE_H - 1 - ny; ly = nx; }
    else { lx = ny; ly = Canvas::NATIVE_W - 1 - nx; }
  }
}

static inline bool px(const uint8_t* plane, int lx, int ly) {
  return plane[ly * Canvas::STRIDE + (lx >> 3)] & (0x80 >> (lx & 7));
}

static void send_plane(uint8_t cmd, uint8_t src, const uint8_t* black, const uint8_t* accent, int rot) {
  tx(&cmd, 1, false);
  const int w = s_b->native_portrait ? Canvas::W : Canvas::NATIVE_W;
  const int h = s_b->native_portrait ? Canvas::H : Canvas::NATIVE_H;
  for (int ny = 0; ny < h; ny++) {
    size_t n;
    if (src == SRC_PALETTE) {
      n = (size_t)w / 2;
      for (int nx = 0; nx < w; nx += 2) {
        uint8_t v = 0;
        for (int k = 0; k < 2; k++) {
          int lx, ly;
          to_logical(nx + k, ny, rot, lx, ly);
          uint8_t c = px(accent, lx, ly) ? s_b->pal_accent : px(black, lx, ly) ? s_b->pal_black : s_b->pal_white;
          v |= (c & 0xF) << (k ? 0 : 4);
        }
        s_row[nx / 2] = v;
      }
    } else {
      const uint8_t* plane = (src == SRC_ACCENT || src == SRC_ACCENT_INV) ? accent : black;
      const bool inv = src == SRC_BLACK_INV || src == SRC_ACCENT_INV;
      n = (size_t)w / 8;
      for (int bx = 0; bx < w / 8; bx++) {
        uint8_t v = 0;
        for (int bit = 0; bit < 8; bit++) {
          int lx, ly;
          to_logical(bx * 8 + bit, ny, rot, lx, ly);
          if (px(plane, lx, ly)) v |= 0x80 >> bit;
        }
        s_row[bx] = inv ? (uint8_t)~v : v;
      }
    }
    tx(s_row, n, true);
  }
}

static void run(Script sc, const uint8_t* black, const uint8_t* accent, int rot) {
  size_t off = 0;
  for (int i = 0; i < sc; i++) off += s_b->len[i];
  const uint8_t* p = s_b->code + off;
  const uint8_t* end = p + s_b->len[sc];
  while (p < end) {
    switch (*p++) {
      case OP_CMD: {
        uint8_t cmd = p[0], n = p[1];
        tx(&cmd, 1, false);
        tx(p + 2, n, true);
        p += 2 + n;
        break;
      }
      case OP_WAIT: wait_busy(*p++); break;
      case OP_DELAY: vTaskDelay(pdMS_TO_TICKS(p[0] | (p[1] << 8)) + 1); p += 2; break;
      case OP_RESET:
        if (s_b->rst >= 0) {
          gpio_set_level((gpio_num_t)s_b->rst, 0);
          vTaskDelay(pdMS_TO_TICKS(10) + 1);
          gpio_set_level((gpio_num_t)s_b->rst, 1);
          vTaskDelay(pdMS_TO_TICKS(10) + 1);
        }
        break;
      case OP_PLANE:
        if (black && accent) send_plane(p[0], p[1], black, accent, rot);
        p += 2;
        break;
      case OP_POWER:
        if (s_b->pwr >= 0) gpio_set_level((gpio_num_t)s_b->pwr, *p);
        p++;
        break;
      default: return;  // board_from_json never emits this; stop rather than guess
    }
  }
}

static void out_pin(int pin, int level) {
  if (pin < 0) return;
  gpio_reset_pin((gpio_num_t)pin);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)pin, level);
}

bool panel_spi_init(const Board& b) {
  s_b = &b;
  s_rep = {};
  if (s_dev) {  // a new board profile arrived over USB: start over
    spi_bus_remove_device(s_dev);
    spi_bus_free(SPI2_HOST);
    s_dev = nullptr;
  }
  spi_bus_config_t bus = {};
  bus.mosi_io_num = b.mosi;
  bus.miso_io_num = -1;
  bus.sclk_io_num = b.sck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = sizeof s_row;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = (int)b.spi_hz;
  dev.mode = 0;
  dev.spics_io_num = b.cs;
  dev.queue_size = 1;
  if (spi_bus_add_device(SPI2_HOST, &dev, &s_dev) != ESP_OK) return false;
  out_pin(b.dc, 1);
  out_pin(b.rst, 1);
  out_pin(b.pwr, 0);
  gpio_reset_pin((gpio_num_t)b.busy);
  gpio_set_direction((gpio_num_t)b.busy, GPIO_MODE_INPUT);
  gpio_set_pull_mode((gpio_num_t)b.busy, b.busy_high ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY);
  s_awake = false;
  ESP_LOGI(TAG, "SPI panel: sck %d mosi %d cs %d dc %d rst %d busy %d @ %lu Hz", b.sck, b.mosi, b.cs, b.dc, b.rst,
           b.busy, (unsigned long)b.spi_hz);
  return true;
}

void panel_spi_present(const uint8_t* black, const uint8_t* accent, Refresh mode, int rotation) {
  if (!s_dev) return;
  int64_t t0 = esp_timer_get_time();
  s_rep.busy_ms = 0;
  s_rep.busy_seen = false;
  if (!s_awake) run(SC_INIT, nullptr, nullptr, rotation);
  s_awake = true;
  run(SC_WRITE, black, accent, rotation);
  run(mode == Refresh::Fast && s_b->len[SC_FAST] ? SC_FAST : SC_FULL, black, accent, rotation);
  if (s_b->len[SC_SLEEP]) {
    run(SC_SLEEP, nullptr, nullptr, rotation);
    s_awake = false;
  }
  s_rep.total_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
  s_rep.frames++;
}

void panel_spi_sleep() {
  if (!s_dev || !s_awake) return;
  run(SC_SLEEP, nullptr, nullptr, 0);
  s_awake = false;
}

PanelReport panel_spi_report() { return s_rep; }

}  // namespace hal
