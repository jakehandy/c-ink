#pragma once
#include <stdint.h>

#include "cache.h"

// The two large scratch buffers, allocated once (in sync.cpp). Sync, the USB
// link and the boot path use them at different times, never concurrently.
extern uint8_t g_blob_buf[CACHE_MAX_BLOB];
extern char g_plain_buf[CACHE_MAX_BLOB];
