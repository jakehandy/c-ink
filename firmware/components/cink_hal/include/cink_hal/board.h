#pragma once
#include <stddef.h>
#include <stdint.h>

// The TICKEY's wiring, as data. ENILINX has not published it, so instead of
// hard-coding pins C-Ink keeps a board profile in NVS that C-Ink Settings can
// write over USB: from tools/tickey-probe (which reads the pin map off the
// stock firmware), from Learn buttons, or by hand. Kconfig values are the
// fallback when no profile is stored.
namespace hal {

enum class PanelType : uint8_t { None = 0, Spi = 1 };
enum class PixelFormat : uint8_t {
  Mono1 = 0,     // 1 bit per pixel, separate black and accent planes (UC81xx / SSD16xx style)
  Palette4 = 1,  // 4 bits per pixel palette codes, two pixels per byte (Spectra 6 style)
};

// Panel command scripts are tiny bytecode programs, so a new controller is a
// new profile, not new firmware.
enum Op : uint8_t {
  OP_END = 0x00,
  OP_CMD = 0x01,    // cmd, n, data[n]: command byte with DC low, then data with DC high
  OP_WAIT = 0x02,   // seconds: wait for BUSY to clear (max)
  OP_DELAY = 0x03,  // ms lo, ms hi
  OP_RESET = 0x04,  // pulse RST low
  OP_PLANE = 0x05,  // cmd, source: send cmd, then the whole frame (see PlaneSrc)
  OP_POWER = 0x06,  // level: drive the panel power-enable pin
};
enum PlaneSrc : uint8_t { SRC_BLACK = 0, SRC_BLACK_INV = 1, SRC_ACCENT = 2, SRC_ACCENT_INV = 3, SRC_PALETTE = 4 };
enum Script : uint8_t { SC_INIT = 0, SC_WRITE, SC_FULL, SC_FAST, SC_SLEEP, SC_COUNT };

struct Board {
  uint32_t magic;
  uint16_t version;
  int8_t btn[3];          // GPIO for up, down, back (-1 unknown)
  bool btn_active_low;
  PanelType panel;
  int8_t sck, mosi, cs, dc, rst, busy, pwr;
  bool busy_high;         // BUSY is asserted high (SSD16xx) rather than low (UC81xx)
  bool native_portrait;   // controller rows are 480 pixels wide
  bool mirror;            // controller scans each row right to left
  PixelFormat format;
  uint8_t pal_white, pal_black, pal_accent;  // Palette4 codes
  uint32_t spi_hz;
  uint16_t len[SC_COUNT];
  uint8_t code[1024];     // the scripts, back to back in Script order
};

void board_defaults(Board& b);   // Kconfig pins, headless panel
bool board_load(Board& b);       // false (and defaults) if none stored
bool board_save(const Board& b);
void board_erase();
const Board& board();            // the active profile (loaded by board_init)
void board_init();

// Profile <-> JSON (the format tools/tickey-probe writes and C-Ink Settings
// sends). Parsing returns nullptr on success or a short reason.
const char* board_from_json(const char* json, Board& out);
// Writes a JSON summary (pins and script sizes) into out.
void board_to_json(const Board& b, char* out, size_t n);

}  // namespace hal
