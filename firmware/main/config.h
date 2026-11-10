#pragma once
#include <stdint.h>

// Everything C-Ink Settings provisions over USB. Lives in NVS.
struct WifiCred {
  char ssid[33];
  char pass[65];
};

struct Config {
  static constexpr int MAX_WIFI = 5;
  uint32_t version;
  bool provisioned;
  WifiCred wifi[MAX_WIFI];
  uint8_t nwifi;
  char bridge_host[64];  // mDNS name of the Mac/PC running C-Ink Settings ("" = discover)
  uint16_t bridge_port;
  char relay_url[128];   // optional: https://... serving the same encrypted blob anywhere
  char token[65];        // bearer token for bridge and relay
  uint8_t key[32];       // AES-256-GCM key; digests are sealed for this device only
  uint16_t refresh_min;  // sync interval, default 15
  uint32_t disabled;     // screens switched off (bit per cink::Kind)
  int8_t quiet_start;    // hour, -1 = no quiet hours
  int8_t quiet_end;
  char tz[64];           // POSIX TZ string
  bool mono;             // never use the accent color
};

void config_defaults(Config& c);
bool config_load(Config& c);
bool config_save(const Config& c);
void config_erase();
const char* device_id();  // "C1A7-22F0", from the factory MAC
