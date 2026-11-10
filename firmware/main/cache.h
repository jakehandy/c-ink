#pragma once
#include <stddef.h>
#include <stdint.h>

// A/B slots on the "cache" partition holding the last encrypted digest blob
// exactly as received. A torn write leaves the other slot intact.
// The bridge keeps digests under 20 KB of JSON; 24 KB leaves headroom for
// the GCM header and tag.
constexpr size_t CACHE_MAX_BLOB = 32 * 1024;  // 20 KB of cards + the story photo

bool cache_save(const uint8_t* blob, size_t len);
// Loads the newest valid blob. Returns its length, or 0 if there is none.
size_t cache_load(uint8_t* out, size_t cap);
void cache_erase();
