#include "cache.h"

#include <string.h>

#include "esp_crc.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char* TAG = "cache";
static const uint32_t kMagic = 0x31434B43;  // "CKC1"

struct Header {
  uint32_t magic;
  uint32_t seq;
  uint32_t len;
  uint32_t crc;
};

static const esp_partition_t* part() {
  static const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "cache");
  return p;
}

static size_t slot_size() { return part() ? part()->size / 2 : 0; }

static bool read_header(int slot, Header& h) {
  if (esp_partition_read(part(), slot * slot_size(), &h, sizeof h) != ESP_OK) return false;
  return h.magic == kMagic && h.len > 0 && h.len <= slot_size() - sizeof h && h.len <= CACHE_MAX_BLOB;
}

size_t cache_load(uint8_t* out, size_t cap) {
  if (!part()) return 0;
  Header h[2];
  bool ok[2] = {read_header(0, h[0]), read_header(1, h[1])};
  int order[2] = {0, 1};
  if (ok[1] && (!ok[0] || h[1].seq > h[0].seq)) { order[0] = 1; order[1] = 0; }
  for (int s : order) {
    if (!ok[s] || h[s].len > cap) continue;
    if (esp_partition_read(part(), s * slot_size() + sizeof(Header), out, h[s].len) != ESP_OK) continue;
    if (esp_crc32_le(0, out, h[s].len) != h[s].crc) {
      ESP_LOGW(TAG, "slot %d failed CRC", s);
      continue;
    }
    return h[s].len;
  }
  return 0;
}

bool cache_save(const uint8_t* blob, size_t len) {
  if (!part() || len == 0 || len > CACHE_MAX_BLOB || len > slot_size() - sizeof(Header)) return false;
  Header h[2];
  bool ok[2] = {read_header(0, h[0]), read_header(1, h[1])};
  uint32_t seq0 = ok[0] ? h[0].seq : 0, seq1 = ok[1] ? h[1].seq : 0;
  // Write over the older (or invalid) slot.
  int slot = !ok[0] ? 0 : !ok[1] ? 1 : (seq0 <= seq1 ? 0 : 1);
  uint32_t seq = (seq0 > seq1 ? seq0 : seq1) + 1;
  size_t off = slot * slot_size();
  size_t span = (sizeof(Header) + len + 4095) & ~(size_t)4095;
  if (esp_partition_erase_range(part(), off, span) != ESP_OK) return false;
  Header nh = {kMagic, seq, (uint32_t)len, esp_crc32_le(0, blob, len)};
  // Body first, header last: a header only exists over a complete body.
  if (esp_partition_write(part(), off + sizeof nh, blob, len) != ESP_OK) return false;
  return esp_partition_write(part(), off, &nh, sizeof nh) == ESP_OK;
}

void cache_erase() {
  if (part()) esp_partition_erase_range(part(), 0, part()->size);
}
