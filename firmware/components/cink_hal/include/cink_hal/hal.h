#pragma once
#include <stdbool.h>
#include <stdint.h>

// Board abstraction for the TICKEY. Everything the app needs from the
// hardware goes through here. Pins and the panel driver come from the board
// profile (board.h), so the port can be finished without new firmware.
namespace hal {

// ---- display
// Frames are the portrait canvas (480x720, 1-bit planes, MSB first, 60 bytes
// per row). Backends rotate to the panel's native scan order themselves.
// rotation 0 = buttons on the right, 1 = flipped 180 degrees.
enum class Refresh : uint8_t {
  Fast,  // ~1 s mono waveform; ghosting builds up over time
  Full,  // ~3-13 s full color waveform; clears ghosting, shows the accent color
};
struct PanelReport {
  uint32_t frames;    // frames presented since display_init
  uint32_t busy_ms;   // time spent waiting on BUSY in the last frame
  uint32_t total_ms;  // wall time of the last frame
  uint16_t timeouts;  // BUSY waits that hit their limit
  bool busy_seen;     // BUSY asserted at least once in the last frame
};
bool display_init();  // call after board_init(); safe to call again after a new profile
void display_present(const uint8_t* black, const uint8_t* accent, Refresh mode, int rotation);
void display_sleep();
const char* display_backend();  // "spi", "vendor" or "headless"
PanelReport display_report();

// ---- buttons
enum Button : uint8_t { BTN_NONE = 0, BTN_UP = 1, BTN_DOWN = 2, BTN_BACK = 4 };
void buttons_init();                // reads pins from board(); call again after a new profile
uint8_t buttons_read();             // bitmask of currently pressed buttons
uint64_t buttons_wake_mask();       // GPIO mask for ext1 wake (0 if pins unknown)
bool buttons_wake_active_low();
void buttons_prepare_sleep();      // RTC pulls + wake source for deep sleep
uint8_t buttons_from_wake_mask(uint64_t pins);  // ext1 wake status -> Button bits

// Pin learning for the Learn buttons wizard: samples every GPIO that is safe
// to read (not flash, PSRAM, USB or a panel pin) with a pull toward idle.
// learn_begin() returns how many pins are being watched; learn_poll() returns
// the next pin whose level changed, or -1.
int learn_begin(bool active_low);
int learn_poll(int* level);
void learn_end();

// ---- power
void power_init();
int battery_percent();              // -1 if unknown
bool charging();
bool usb_connected();               // host is talking to us over USB-C

}  // namespace hal
