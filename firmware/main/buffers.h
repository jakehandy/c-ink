#pragma once
#include <stdint.h>

#include "cache.h"

// The one large scratch buffer (defined in sync.cpp). Sync, the USB link and
// the boot path use it at different times, never concurrently. Sealed blobs
// are verified by streaming and opened in place, so there is no second
// plaintext buffer.
extern uint8_t g_blob_buf[CACHE_MAX_BLOB];
