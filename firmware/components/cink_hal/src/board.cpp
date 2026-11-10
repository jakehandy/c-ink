#include "cink_hal/board.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace hal {

static const uint32_t kMagic = 0xB0A2D001;
static const uint16_t kVersion = 1;
static Board s_board;

void board_defaults(Board& b) {
  memset(&b, 0, sizeof b);
  b.magic = kMagic;
  b.version = kVersion;
  b.btn[0] = CONFIG_CINK_PIN_BTN_UP;
  b.btn[1] = CONFIG_CINK_PIN_BTN_DOWN;
  b.btn[2] = CONFIG_CINK_PIN_BTN_BACK;
#if CONFIG_CINK_BTN_ACTIVE_LOW
  b.btn_active_low = true;
#endif
  b.panel = PanelType::None;
  b.sck = b.mosi = b.cs = b.dc = b.rst = b.busy = b.pwr = -1;
  b.spi_hz = 10 * 1000 * 1000;
  b.pal_white = 1;
  b.pal_black = 0;
  b.pal_accent = 3;
}

bool board_load(Board& b) {
  nvs_handle_t h;
  board_defaults(b);
  if (nvs_open("cink", NVS_READONLY, &h) != ESP_OK) return false;
  Board tmp;
  size_t n = sizeof tmp;
  esp_err_t e = nvs_get_blob(h, "board", &tmp, &n);
  nvs_close(h);
  if (e != ESP_OK || n != sizeof tmp || tmp.magic != kMagic || tmp.version != kVersion) return false;
  b = tmp;
  return true;
}

bool board_save(const Board& b) {
  nvs_handle_t h;
  if (nvs_open("cink", NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t e = nvs_set_blob(h, "board", &b, sizeof b);
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  if (e == ESP_OK) s_board = b;
  return e == ESP_OK;
}

void board_erase() {
  nvs_handle_t h;
  if (nvs_open("cink", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_key(h, "board");
  nvs_commit(h);
  nvs_close(h);
  board_defaults(s_board);
}

void board_init() { board_load(s_board); }
const Board& board() { return s_board; }

// ------------------------------------------------------------------ JSON

static int pin(const cJSON* o, const char* k, int def = -1) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  if (!cJSON_IsNumber(v)) return def;
  int p = (int)v->valuedouble;
  return p >= -1 && p <= 48 ? p : def;
}

static bool flag(const cJSON* o, const char* k, bool def = false) {
  const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

static int byte_of(const cJSON* v) {
  if (cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble <= 255) return (int)v->valuedouble;
  return -1;
}

// ["cmd", 0x10, [1, 2]] | ["wait", 30] | ["delay", 20] | ["reset"] | ["plane", 0x10, "black"] | ["power", 1]
static const char* compile(const cJSON* ops, uint8_t* out, size_t cap, uint16_t& len) {
  len = 0;
  if (!ops) return nullptr;
  if (!cJSON_IsArray(ops)) return "script is not a list";
  auto put = [&](int b) {
    if (len >= cap || b < 0 || b > 255) return false;
    out[len++] = (uint8_t)b;
    return true;
  };
  const cJSON* op;
  cJSON_ArrayForEach(op, ops) {
    const cJSON* name = cJSON_GetArrayItem(op, 0);
    if (!cJSON_IsString(name)) return "step without a name";
    const char* n = name->valuestring;
    const cJSON* a1 = cJSON_GetArrayItem(op, 1);
    const cJSON* a2 = cJSON_GetArrayItem(op, 2);
    bool ok;
    if (!strcmp(n, "cmd")) {
      int nd = cJSON_IsArray(a2) ? cJSON_GetArraySize(a2) : 0;
      ok = put(OP_CMD) && put(byte_of(a1)) && put(nd);
      for (int i = 0; ok && i < nd; i++) ok = put(byte_of(cJSON_GetArrayItem(a2, i)));
    } else if (!strcmp(n, "wait")) {
      ok = put(OP_WAIT) && put(cJSON_IsNumber(a1) ? byte_of(a1) : 30);
    } else if (!strcmp(n, "delay")) {
      int ms = cJSON_IsNumber(a1) ? (int)a1->valuedouble : 10;
      ok = ms >= 0 && ms <= 65535 && put(OP_DELAY) && put(ms & 0xFF) && put(ms >> 8);
    } else if (!strcmp(n, "reset")) {
      ok = put(OP_RESET);
    } else if (!strcmp(n, "power")) {
      ok = put(OP_POWER) && put(cJSON_IsNumber(a1) && a1->valuedouble ? 1 : 0);
    } else if (!strcmp(n, "plane")) {
      const char* s = cJSON_IsString(a2) ? a2->valuestring : "";
      int src = !strcmp(s, "black") ? SRC_BLACK : !strcmp(s, "black_inv") ? SRC_BLACK_INV
              : !strcmp(s, "accent") ? SRC_ACCENT : !strcmp(s, "accent_inv") ? SRC_ACCENT_INV
              : !strcmp(s, "palette") ? SRC_PALETTE : -1;
      ok = put(OP_PLANE) && put(byte_of(a1)) && put(src);
    } else {
      return "unknown step";
    }
    if (!ok) return "script too long or bad value";
  }
  return nullptr;
}

const char* board_from_json(const char* json, Board& b) {
  cJSON* root = cJSON_Parse(json);
  if (!root) return "not JSON";
  board_defaults(b);
  const char* err = nullptr;
  const cJSON* btn = cJSON_GetObjectItemCaseSensitive(root, "buttons");
  if (btn) {
    b.btn[0] = (int8_t)pin(btn, "up");
    b.btn[1] = (int8_t)pin(btn, "down");
    b.btn[2] = (int8_t)pin(btn, "back");
    b.btn_active_low = flag(btn, "active_low", true);
  }
  const cJSON* p = cJSON_GetObjectItemCaseSensitive(root, "panel");
  const cJSON* type = p ? cJSON_GetObjectItemCaseSensitive(p, "type") : nullptr;
  if (cJSON_IsString(type) && !strcmp(type->valuestring, "spi")) {
    b.panel = PanelType::Spi;
    b.sck = (int8_t)pin(p, "sck");
    b.mosi = (int8_t)pin(p, "mosi");
    b.cs = (int8_t)pin(p, "cs");
    b.dc = (int8_t)pin(p, "dc");
    b.rst = (int8_t)pin(p, "rst");
    b.busy = (int8_t)pin(p, "busy");
    b.pwr = (int8_t)pin(p, "pwr");
    b.busy_high = flag(p, "busy_high");
    b.native_portrait = flag(p, "native_portrait");
    b.mirror = flag(p, "mirror");
    const cJSON* hz = cJSON_GetObjectItemCaseSensitive(p, "hz");
    if (cJSON_IsNumber(hz) && hz->valuedouble >= 100000 && hz->valuedouble <= 40000000) b.spi_hz = (uint32_t)hz->valuedouble;
    const cJSON* fmt = cJSON_GetObjectItemCaseSensitive(p, "format");
    if (cJSON_IsString(fmt) && !strcmp(fmt->valuestring, "palette4")) b.format = PixelFormat::Palette4;
    const cJSON* pal = cJSON_GetObjectItemCaseSensitive(p, "palette");
    if (pal) {
      b.pal_white = (uint8_t)(pin(pal, "white", 1) & 0xF);
      b.pal_black = (uint8_t)(pin(pal, "black", 0) & 0xF);
      b.pal_accent = (uint8_t)(pin(pal, "accent", 3) & 0xF);
    }
    if (b.sck < 0 || b.mosi < 0 || b.dc < 0 || b.busy < 0) err = "SPI panel needs sck, mosi, dc and busy";
    static const char* const names[SC_COUNT] = {"init", "write", "full", "fast", "sleep"};
    size_t used = 0;
    for (int i = 0; !err && i < SC_COUNT; i++) {
      err = compile(cJSON_GetObjectItemCaseSensitive(p, names[i]), b.code + used, sizeof b.code - used, b.len[i]);
      used += b.len[i];
    }
    if (!err && (!b.len[SC_WRITE] || !b.len[SC_FULL])) err = "SPI panel needs write and full scripts";
  } else if (type && !(cJSON_IsString(type) && !strcmp(type->valuestring, "none"))) {
    err = "only \"spi\" and \"none\" panels are supported so far";
  }
  cJSON_Delete(root);
  return err;
}

void board_to_json(const Board& b, char* out, size_t n) {
  snprintf(out, n,
           "{\"buttons\":{\"up\":%d,\"down\":%d,\"back\":%d,\"active_low\":%s},"
           "\"panel\":{\"type\":\"%s\",\"sck\":%d,\"mosi\":%d,\"cs\":%d,\"dc\":%d,\"rst\":%d,\"busy\":%d,\"pwr\":%d,"
           "\"hz\":%lu,\"format\":\"%s\",\"script_bytes\":[%u,%u,%u,%u,%u]}}",
           b.btn[0], b.btn[1], b.btn[2], b.btn_active_low ? "true" : "false",
           b.panel == PanelType::Spi ? "spi" : "none", b.sck, b.mosi, b.cs, b.dc, b.rst, b.busy, b.pwr,
           (unsigned long)b.spi_hz, b.format == PixelFormat::Palette4 ? "palette4" : "mono1", b.len[0], b.len[1],
           b.len[2], b.len[3], b.len[4]);
}

}  // namespace hal
