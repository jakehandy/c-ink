#include "net.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char* TAG = "net";
static EventGroupHandle_t s_ev;
static esp_netif_t* s_netif;
static bool s_inited;
static const int BIT_UP = 1, BIT_FAIL = 2;

struct FastConnect {
  uint32_t magic;
  int8_t idx;
  uint8_t channel;
  uint8_t bssid[6];
};
RTC_DATA_ATTR static FastConnect s_fast;
RTC_DATA_ATTR static int64_t s_last_ntp;

static void on_event(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) xEventGroupSetBits(s_ev, BIT_FAIL);
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(s_ev, BIT_UP);
}

static void init_once() {
  if (s_inited) return;
  s_inited = true;
  s_ev = xEventGroupCreate();
  esp_netif_init();
  esp_event_loop_create_default();
  s_netif = esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr);
  esp_wifi_set_mode(WIFI_MODE_STA);
}

static bool try_connect(const WifiCred& w, const FastConnect* fast, int timeout_ms) {
  wifi_config_t wc = {};
  strncpy((char*)wc.sta.ssid, w.ssid, sizeof wc.sta.ssid);
  strncpy((char*)wc.sta.password, w.pass, sizeof wc.sta.password);
  wc.sta.threshold.authmode = w.pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  wc.sta.pmf_cfg.capable = true;
  if (fast) {
    wc.sta.channel = fast->channel;
    memcpy(wc.sta.bssid, fast->bssid, 6);
    wc.sta.bssid_set = true;
  }
  esp_wifi_set_config(WIFI_IF_STA, &wc);
  xEventGroupClearBits(s_ev, BIT_UP | BIT_FAIL);
  esp_wifi_connect();
  EventBits_t b = xEventGroupWaitBits(s_ev, BIT_UP | BIT_FAIL, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
  if (b & BIT_UP) return true;
  esp_wifi_disconnect();
  return false;
}

static void remember(int idx, NetInfo& info) {
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    s_fast.magic = 0xC1C1;
    s_fast.idx = (int8_t)idx;
    s_fast.channel = ap.primary;
    memcpy(s_fast.bssid, ap.bssid, 6);
    strncpy(info.ssid, (const char*)ap.ssid, sizeof info.ssid - 1);
  }
  esp_netif_ip_info_t ip;
  if (esp_netif_get_ip_info(s_netif, &ip) == ESP_OK) snprintf(info.ip, sizeof info.ip, IPSTR, IP2STR(&ip.ip));
}

bool net_up(const Config& c, int timeout_ms, NetInfo& info) {
  memset(&info, 0, sizeof info);
  if (!c.nwifi) return false;
  init_once();
  if (esp_wifi_start() != ESP_OK) return false;

  // 1) Last good network, straight to its BSSID and channel.
  if (s_fast.magic == 0xC1C1 && s_fast.idx >= 0 && s_fast.idx < c.nwifi) {
    if (try_connect(c.wifi[s_fast.idx], &s_fast, 4000)) { remember(s_fast.idx, info); return true; }
    s_fast.magic = 0;
  }
  // 2) Scan, then try known networks from strongest to weakest.
  wifi_scan_config_t sc = {};
  if (esp_wifi_scan_start(&sc, true) != ESP_OK) return false;
  uint16_t n = 20;
  static wifi_ap_record_t aps[20];
  esp_wifi_scan_get_ap_records(&n, aps);  // sorted by RSSI
  for (int i = 0; i < n; i++) {
    for (int k = 0; k < c.nwifi; k++) {
      if (strcmp((const char*)aps[i].ssid, c.wifi[k].ssid) != 0) continue;
      ESP_LOGI(TAG, "trying %s (%d dBm)", c.wifi[k].ssid, aps[i].rssi);
      if (try_connect(c.wifi[k], nullptr, timeout_ms)) { remember(k, info); return true; }
    }
  }
  ESP_LOGW(TAG, "no known network reachable");
  esp_wifi_stop();
  return false;
}

void net_down() {
  if (!s_inited) return;
  esp_wifi_disconnect();
  esp_wifi_stop();
}

bool net_time(int timeout_ms) {
  time_t now = time(nullptr);
  bool valid = now > 1767225600;  // 2026-01-01
  if (valid && now - s_last_ntp < 24 * 3600) return true;
  esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&cfg);
  bool ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
  esp_netif_sntp_deinit();
  if (ok) s_last_ntp = time(nullptr);
  return ok || valid;
}
