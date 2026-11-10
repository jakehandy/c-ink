// Tiny palette-PNG writer (stored deflate, no zlib dependency) for the simulator.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <vector>

namespace png {

inline uint32_t crc(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
  static uint32_t t[256];
  if (!t[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t v = i;
      for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      t[i] = v;
    }
  for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return c;
}

inline void be32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

inline void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> b;
  be32(b, (uint32_t)data.size());
  b.insert(b.end(), type, type + 4);
  b.insert(b.end(), data.begin(), data.end());
  uint32_t c = crc(b.data() + 4, b.size() - 4) ^ 0xFFFFFFFFu;
  be32(b, c);
  fwrite(b.data(), 1, b.size(), f);
}

// pixels: w*h palette indices; pal: rgb triplets.
inline bool write(const char* path, int w, int h, const uint8_t* pixels, const uint8_t* pal, int npal) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, w); be32(ihdr, h);
  ihdr.push_back(8); ihdr.push_back(3); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  chunk(f, "IHDR", ihdr);
  chunk(f, "PLTE", std::vector<uint8_t>(pal, pal + npal * 3));
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), pixels + y * w, pixels + (y + 1) * w);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  for (size_t off = 0; off < raw.size(); off += 65535) {
    size_t n = raw.size() - off < 65535 ? raw.size() - off : 65535;
    z.push_back(off + n == raw.size() ? 1 : 0);
    z.push_back(n & 0xFF); z.push_back(n >> 8);
    z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
  }
  be32(z, (b << 16) | a);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
  return true;
}

}  // namespace png
