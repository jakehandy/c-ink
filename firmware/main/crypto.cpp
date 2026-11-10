#include "crypto.h"

#include <string.h>

#include "mbedtls/gcm.h"

int digest_open(const uint8_t* blob, size_t len, const uint8_t key[32], const char* device_id, char* out, size_t cap) {
  const size_t hdr = 5, nonce = 12, tag = 16;
  if (len < hdr + nonce + tag || memcmp(blob, "CINK\x01", hdr) != 0) return -1;
  size_t clen = len - hdr - nonce - tag;
  if (clen + 1 > cap) return -1;
  uint8_t aad[5 + 16];
  size_t idlen = strlen(device_id);
  if (idlen > 16) idlen = 16;
  memcpy(aad, blob, hdr);
  memcpy(aad + hdr, device_id, idlen);
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int rc = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (rc == 0)
    rc = mbedtls_gcm_auth_decrypt(&g, clen, blob + hdr, nonce, aad, hdr + idlen, blob + len - tag, tag,
                                  blob + hdr + nonce, (unsigned char*)out);
  mbedtls_gcm_free(&g);
  if (rc != 0) return -1;
  out[clen] = 0;
  return (int)clen;
}
