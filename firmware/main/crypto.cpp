#include "crypto.h"

#include <string.h>

#include "mbedtls/gcm.h"

namespace {

constexpr size_t kHdr = 5, kNonce = 12, kTag = 16, kChunk = 256;

// Runs GCM decryption over the ciphertext in chunks. With write_back the
// plaintext replaces the ciphertext; without it, it is dropped as it goes.
bool run(uint8_t* blob, size_t len, const uint8_t key[32], const char* device_id, bool write_back) {
  if (len < kHdr + kNonce + kTag || memcmp(blob, "CINK\x01", kHdr) != 0) return false;
  const size_t clen = len - kHdr - kNonce - kTag;
  uint8_t aad[kHdr + 16];
  size_t idlen = strlen(device_id);
  if (idlen > 16) idlen = 16;
  memcpy(aad, blob, kHdr);
  memcpy(aad + kHdr, device_id, idlen);

  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  uint8_t buf[kChunk];
  uint8_t tag[kTag];
  size_t out = 0;
  bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
            mbedtls_gcm_starts(&g, MBEDTLS_GCM_DECRYPT, blob + kHdr, kNonce) == 0 &&
            mbedtls_gcm_update_ad(&g, aad, kHdr + idlen) == 0;
  uint8_t* ct = blob + kHdr + kNonce;
  for (size_t off = 0; ok && off < clen; off += kChunk) {
    size_t n = clen - off < kChunk ? clen - off : kChunk;
    ok = mbedtls_gcm_update(&g, ct + off, n, buf, sizeof buf, &out) == 0 && out == n;
    if (ok && write_back) memcpy(ct + off, buf, n);
  }
  ok = ok && mbedtls_gcm_finish(&g, buf, sizeof buf, &out, tag, kTag) == 0;
  mbedtls_gcm_free(&g);
  memset(buf, 0, sizeof buf);

  // Constant-time tag compare.
  uint8_t diff = ok ? 0 : 1;
  const uint8_t* want = blob + len - kTag;
  for (size_t i = 0; i < kTag; i++) diff |= tag[i] ^ want[i];
  if (diff && write_back) memset(ct, 0, clen);
  return diff == 0;
}

}  // namespace

bool digest_verify(const uint8_t* blob, size_t len, const uint8_t key[32], const char* device_id) {
  return run(const_cast<uint8_t*>(blob), len, key, device_id, false);
}

int digest_open_inplace(uint8_t* blob, size_t len, const uint8_t key[32], const char* device_id, char** plain) {
  if (!run(blob, len, key, device_id, true)) return -1;
  size_t clen = len - kHdr - kNonce - kTag;
  char* p = reinterpret_cast<char*>(blob + kHdr + kNonce);
  p[clen] = 0;  // lands on the (already checked) tag
  *plain = p;
  return (int)clen;
}
