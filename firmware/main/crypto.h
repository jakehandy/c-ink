#pragma once
#include <stddef.h>
#include <stdint.h>

// Digest blobs: "CINK" | 0x01 | nonce[12] | ciphertext | tag[16], AES-256-GCM,
// AAD = the 5-byte header followed by the device id. The bridge seals each
// digest for one device, so neither the LAN, the relay, nor the flash cache
// ever holds readable personal data.
// Returns plaintext length (NUL-terminated in out), or -1.
int digest_open(const uint8_t* blob, size_t len, const uint8_t key[32], const char* device_id, char* out, size_t cap);
