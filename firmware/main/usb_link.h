#pragma once
#include <stdint.h>

#include "config.h"

// Line protocol with C-Ink Settings over USB-C (USB Serial/JTAG). The host
// sends one JSON object per line; replies are lines starting with "@cink "
// so they can share the wire with log output. See docs/protocol.md.

struct UsbStatus {
  int battery;
  bool charging;
  const char* ssid;
  const char* ip;
  int64_t last_sync;
  int cards;
  const char* firmware;
};

enum class UsbEvent : uint8_t {
  None,
  ConfigChanged,  // config saved; re-read it
  DigestPushed,   // a sealed digest arrived over USB and is in the cache
  SyncNow,
  PressUp,
  PressDown,
  PressBack,
  ShowInfo,
  Reset,          // factory reset done; reboot
};

// `black`/`accent` are the app's frame planes, read back by the "frame" op
// (the panel backends keep no copy).
void usb_init(const uint8_t* black, const uint8_t* accent);
UsbEvent usb_poll(Config& cfg, const UsbStatus& st);
