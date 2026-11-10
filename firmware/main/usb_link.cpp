#include "usb_link.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "cJSON.h"
#include "buffers.h"
#include "cache.h"
#include "cink_hal/hal.h"
#include "crypto.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "mbedtls/base64.h"
#include "sync.h"

static char s_line[8192];
static size_t s_len;
static bool s_drop;  // current line overflowed; discard until newline
#define s_blob g_blob_buf
#define s_plain g_plain_buf
static size_t s_blob_len, s_blob_want;

void usb_init() {
  usb_serial_jtag_driver_config_t c = {};
  c.rx_buffer_size = 4096;
  c.tx_buffer_size = 4096;
  if (!usb_serial_jtag_is_driver_installed()) usb_serial_jtag_driver_install(&c);
}

static void reply(cJSON* o) {
  char* s = cJSON_PrintUnformatted(o);
  if (s) {
    usb_serial_jtag_write_bytes("@cink ", 6, pdMS_TO_TICKS(200));
    usb_serial_jtag_write_bytes(s, strlen(s), pdMS_TO_TICKS(500));
    usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(200));
    cJSON_free(s);
  }
  cJSON_Delete(o);
}

static void reply_ok(const char* op, bool ok, const char* err = nullptr) {
  cJSON* o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "op", op);
  cJSON_AddBoolToObject(o, "ok", ok);
  if (err) cJSON_AddStringToObject(o, "error", err);
  reply(o);
}

static const char* str(cJSON* o, const char* k) {
  cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsString(v) ? v->valuestring : nullptr;
}

static void copy(char* dst, size_t n, const char* src) {
  if (!src) return;
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

static bool apply_config(cJSON* m, Config& c, const char** err) {
  cJSON* wifi = cJSON_GetObjectItemCaseSensitive(m, "wifi");
  if (cJSON_IsArray(wifi)) {
    c.nwifi = 0;
    cJSON* w;
    cJSON_ArrayForEach(w, wifi) {
      if (c.nwifi >= Config::MAX_WIFI) break;
      const char* ssid = str(w, "ssid");
      if (!ssid || !*ssid) continue;
      WifiCred& wc = c.wifi[c.nwifi++];
      memset(&wc, 0, sizeof wc);
      copy(wc.ssid, sizeof wc.ssid, ssid);
      copy(wc.pass, sizeof wc.pass, str(w, "pass"));
    }
  }
  copy(c.bridge_host, sizeof c.bridge_host, str(m, "bridge_host"));
  copy(c.relay_url, sizeof c.relay_url, str(m, "relay_url"));
  copy(c.token, sizeof c.token, str(m, "token"));
  copy(c.tz, sizeof c.tz, str(m, "tz"));
  const char* key = str(m, "key");
  if (key) {
    size_t n = 0;
    uint8_t k[48];
    if (mbedtls_base64_decode(k, sizeof k, &n, (const unsigned char*)key, strlen(key)) != 0 || n != 32) {
      *err = "key must be 32 bytes, base64";
      return false;
    }
    memcpy(c.key, k, 32);
  }
  cJSON* v;
  // The host's clock, so a device that has never reached NTP still knows
  // what time it is (needed for countdowns on a USB-pushed digest).
  if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(m, "time")) && v->valuedouble > 1767225600) {
    struct timeval tv = {(time_t)v->valuedouble, 0};
    settimeofday(&tv, nullptr);
  }
  if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(m, "bridge_port"))) c.bridge_port = (uint16_t)v->valueint;
  if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(m, "refresh_min"))) {
    int r = v->valueint;
    c.refresh_min = (uint16_t)(r < 5 ? 5 : r > 240 ? 240 : r);
  }
  if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(m, "disabled"))) c.disabled = (uint32_t)v->valuedouble;
  if (cJSON_IsBool(v = cJSON_GetObjectItemCaseSensitive(m, "mono"))) c.mono = cJSON_IsTrue(v);
  v = cJSON_GetObjectItemCaseSensitive(m, "quiet");
  if (cJSON_IsNull(v)) { c.quiet_start = c.quiet_end = -1; }
  else if (cJSON_IsArray(v) && cJSON_GetArraySize(v) == 2) {
    c.quiet_start = (int8_t)cJSON_GetArrayItem(v, 0)->valueint;
    c.quiet_end = (int8_t)cJSON_GetArrayItem(v, 1)->valueint;
  }
  bool has_key = false;
  for (int i = 0; i < 32; i++) has_key |= c.key[i] != 0;
  c.provisioned = has_key && c.token[0] && (c.nwifi > 0 || c.relay_url[0]);
  return true;
}

static void send_plane(const char* name, const uint8_t* p) {
  const size_t total = 720 / 8 * 480, chunk = 2400;
  static unsigned char b64[3300];
  for (size_t off = 0; off < total; off += chunk) {
    size_t n = 0;
    mbedtls_base64_encode(b64, sizeof b64, &n, p + off, chunk);
    b64[n] = 0;
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "op", "frame");
    cJSON_AddStringToObject(o, "plane", name);
    cJSON_AddNumberToObject(o, "off", (double)off);
    cJSON_AddNumberToObject(o, "total", (double)total);
    cJSON_AddStringToObject(o, "b64", (const char*)b64);
    reply(o);
  }
}

static UsbEvent handle(const char* line, Config& cfg, const UsbStatus& st) {
  cJSON* m = cJSON_Parse(line);
  if (!m) return UsbEvent::None;
  const char* op = str(m, "op");
  UsbEvent ev = UsbEvent::None;
  if (!op) {
  } else if (!strcmp(op, "hello") || !strcmp(op, "status")) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "op", op);
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddStringToObject(o, "device", device_id());
    cJSON_AddStringToObject(o, "firmware", st.firmware);
    cJSON_AddBoolToObject(o, "provisioned", cfg.provisioned);
    cJSON_AddNumberToObject(o, "battery", st.battery);
    cJSON_AddBoolToObject(o, "charging", st.charging);
    cJSON_AddStringToObject(o, "ssid", st.ssid ? st.ssid : "");
    cJSON_AddStringToObject(o, "ip", st.ip ? st.ip : "");
    cJSON_AddNumberToObject(o, "last_sync", (double)st.last_sync);
    cJSON_AddNumberToObject(o, "cards", st.cards);
    cJSON_AddNumberToObject(o, "refresh_min", cfg.refresh_min);
    cJSON_AddNumberToObject(o, "networks", cfg.nwifi);
    reply(o);
  } else if (!strcmp(op, "config")) {
    const char* err = nullptr;
    Config next = cfg;
    if (apply_config(m, next, &err) && config_save(next)) {
      cfg = next;
      reply_ok(op, true);
      ev = UsbEvent::ConfigChanged;
    } else {
      reply_ok(op, false, err ? err : "could not save");
    }
  } else if (!strcmp(op, "blob_begin")) {
    cJSON* n = cJSON_GetObjectItemCaseSensitive(m, "len");
    s_blob_len = 0;
    s_blob_want = cJSON_IsNumber(n) ? (size_t)n->valuedouble : 0;
    reply_ok(op, s_blob_want > 0 && s_blob_want <= sizeof s_blob, s_blob_want > sizeof s_blob ? "too large" : nullptr);
  } else if (!strcmp(op, "blob_data")) {
    const char* b = str(m, "b64");
    size_t n = 0;
    bool ok = b && s_blob_want &&
              mbedtls_base64_decode(s_blob + s_blob_len, sizeof s_blob - s_blob_len, &n, (const unsigned char*)b, strlen(b)) == 0;
    if (ok) s_blob_len += n;
    reply_ok(op, ok);
  } else if (!strcmp(op, "blob_end")) {
    bool ok = s_blob_len == s_blob_want && digest_open(s_blob, s_blob_len, cfg.key, device_id(), s_plain, sizeof s_plain) >= 0;
    ok = ok && cache_save(s_blob, s_blob_len);
    if (ok) sync_forget_etag();
    reply_ok(op, ok, ok ? nullptr : "digest did not verify");
    s_blob_want = 0;
    if (ok) ev = UsbEvent::DigestPushed;
  } else if (!strcmp(op, "frame")) {
    send_plane("black", hal::display_last_black());
    send_plane("accent", hal::display_last_accent());
    reply_ok("frame_end", true);
  } else if (!strcmp(op, "sync")) {
    reply_ok(op, true);
    ev = UsbEvent::SyncNow;
  } else if (!strcmp(op, "press")) {
    const char* b = str(m, "button");
    if (b && !strcmp(b, "up")) ev = UsbEvent::PressUp;
    else if (b && !strcmp(b, "down")) ev = UsbEvent::PressDown;
    else if (b && !strcmp(b, "back")) ev = UsbEvent::PressBack;
    else if (b && !strcmp(b, "info")) ev = UsbEvent::ShowInfo;
    reply_ok(op, ev != UsbEvent::None);
  } else if (!strcmp(op, "reset")) {
    config_erase();
    cache_erase();
    reply_ok(op, true);
    ev = UsbEvent::Reset;
  } else {
    reply_ok(op, false, "unknown op");
  }
  cJSON_Delete(m);
  return ev;
}

UsbEvent usb_poll(Config& cfg, const UsbStatus& st) {
  // Bytes already read but not yet consumed survive between calls, so a
  // command that raises an event never swallows the next one.
  static uint8_t rx[256];
  static int rx_n, rx_i;
  for (;;) {
    if (rx_i >= rx_n) {
      rx_n = usb_serial_jtag_read_bytes(rx, sizeof rx, 0);
      rx_i = 0;
      if (rx_n <= 0) { rx_n = 0; return UsbEvent::None; }
    }
    while (rx_i < rx_n) {
      char ch = (char)rx[rx_i++];
      if (ch == '\n' || ch == '\r') {
        bool have = !s_drop && s_len;
        s_line[s_len] = 0;
        s_len = 0;
        s_drop = false;
        if (have) {
          UsbEvent ev = handle(s_line, cfg, st);
          if (ev != UsbEvent::None) return ev;
        }
      } else if (s_len + 1 < sizeof s_line) {
        s_line[s_len++] = ch;
      } else {
        s_drop = true;
      }
    }
  }
}
