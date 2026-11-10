#pragma once
#include <stddef.h>
#include <stdint.h>

// Digest blobs: "CINK" | 0x01 | nonce[12] | ciphertext | tag[16], AES-256-GCM,
// AAD = the 5-byte header followed by the device id. The bridge seals each
// digest for one device, so neither the LAN, the relay, nor the flash cache
// ever holds readable personal data.
//
// Both calls work without a second plaintext buffer (RAM is tight): the
// ciphertext is streamed through a small stack buffer.

// Checks the tag without keeping any plaintext. True if the blob is ours.
bool digest_verify(const uint8_t* blob, size_t len, const uint8_t key[32], const char* device_id);

// Decrypts in place. On success returns the plaintext length and points
// *plain at it (NUL-terminated, inside blob). On failure returns -1 and wipes
// whatever was decrypted.
int digest_open_inplace(uint8_t* blob, size_t len, const uint8_t key[32], const char* device_id, char** plain);
