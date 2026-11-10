#pragma once
#include <stdint.h>

#include "config.h"

struct NetInfo {
  char ssid[33];
  char ip[16];
};

// Connects to the strongest provisioned network (or the last good one first,
// with its cached channel, which saves most of the scan cost on a wake).
bool net_up(const Config& c, int timeout_ms, NetInfo& info);
void net_down();
// Sets the clock over SNTP if it is unset or older than a day. Returns true if
// the clock is trustworthy afterwards.
bool net_time(int timeout_ms);
