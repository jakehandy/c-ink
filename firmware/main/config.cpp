#include "config.h"

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "nvs.h"

static const uint32_t kVersion = 2;  // 2: rotation

void config_defaults(Config& c) {
  memset(&c, 0, sizeof c);
  c.version = kVersion;
  c.bridge_port = 7787;
  c.refresh_min = 15;
  c.quiet_start = 22;
  c.quiet_end = 7;
  strcpy(c.tz, "UTC0");
}

bool config_load(Config& c) {
  config_defaults(c);
  nvs_handle_t h;
  if (nvs_open("cink", NVS_READONLY, &h) != ESP_OK) return false;
  Config tmp;
  size_t n = sizeof tmp;
  esp_err_t e = nvs_get_blob(h, "cfg", &tmp, &n);
  nvs_close(h);
  if (e != ESP_OK || n != sizeof tmp || tmp.version != kVersion) return false;
  c = tmp;
  if (c.refresh_min < 5) c.refresh_min = 5;
  if (c.refresh_min > 240) c.refresh_min = 240;
  return true;
}

bool config_save(const Config& c) {
  nvs_handle_t h;
  if (nvs_open("cink", NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t e = nvs_set_blob(h, "cfg", &c, sizeof c);
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}

void config_erase() {
  nvs_handle_t h;
  if (nvs_open("cink", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_all(h);
  nvs_commit(h);
  nvs_close(h);
}

const char* device_id() {
  static char id[12];
  if (!id[0]) {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    snprintf(id, sizeof id, "%02X%02X-%02X%02X", mac[2], mac[3], mac[4], mac[5]);
  }
  return id;
}
