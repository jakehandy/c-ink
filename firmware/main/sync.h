#pragma once
#include <stddef.h>
#include <stdint.h>

#include "config.h"

enum class SyncResult { Updated, Unchanged, NoNetwork, Failed };

// Brings Wi-Fi up, fetches the sealed digest (C-Ink Settings on the LAN
// first, then the relay), verifies it opens with this device's key, and
// stores it in the flash cache. Wi-Fi is off again when this returns.
SyncResult sync_now(const Config& c, char* ssid_out, char* ip_out, char* source_out);

// Drops the cached ETag so the next sync downloads the digest in full.
void sync_forget_etag();
