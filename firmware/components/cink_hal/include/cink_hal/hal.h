#pragma once
#include <stdbool.h>
#include <stdint.h>

// Board abstraction for the TICKEY. Everything the app needs from the
// hardware goes through here, so finishing the port is one file per
// peripheral once ENILINX publishes pinouts and the panel driver.
namespace hal {

// ---- display (720x480, 1-bit planes, MSB first, 90 bytes per row)
enum class Refresh : uint8_t {
  Fast,  // ~1 s mono waveform; ghosting builds up over time
  Full,  // ~3-13 s full color waveform; clears ghosting, shows the accent color
};
bool display_init();
void display_present(const uint8_t* black, const uint8_t* accent, Refresh mode);
void display_sleep();
// Last presented frame (headless backend keeps it for the USB frame dump).
const uint8_t* display_last_black();
const uint8_t* display_last_accent();

// ---- buttons
enum Button : uint8_t { BTN_NONE = 0, BTN_UP = 1, BTN_DOWN = 2, BTN_BACK = 4 };
void buttons_init();
uint8_t buttons_read();             // bitmask of currently pressed buttons
uint64_t buttons_wake_mask();       // GPIO mask for ext1 wake (0 if pins unknown)
bool buttons_wake_active_low();
void buttons_prepare_sleep();      // RTC pulls + wake source for deep sleep
uint8_t buttons_from_wake_mask(uint64_t pins);  // ext1 wake status -> Button bits

// ---- power
void power_init();
int battery_percent();              // -1 if unknown
bool charging();
bool usb_connected();               // host is talking to us over USB-C

}  // namespace hal
