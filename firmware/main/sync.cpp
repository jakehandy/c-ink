#include "sync.h"

#include <stdio.h>
#include <string.h>

#include "buffers.h"
#include "cache.h"
#include "crypto.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mdns.h"
#include "net.h"

static const char* TAG = "sync";
uint8_t g_blob_buf[CACHE_MAX_BLOB];
char g_plain_buf[CACHE_MAX_BLOB];
#define s_blob g_blob_buf
#define s_plain g_plain_buf
RTC_DATA_ATTR static char s_etag[40];

struct Body {
  size_t len;
  bool overflow;
};

static esp_err_t on_http(esp_http_client_event_t* e) {
  Body* b = (Body*)e->user_data;
  if (e->event_id == HTTP_EVENT_ON_DATA) {
    if (b->len + e->data_len > sizeof s_blob) { b->overflow = true; return ESP_OK; }
    memcpy(s_blob + b->len, e->data, e->data_len);
    b->len += e->data_len;
  } else if (e->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(e->header_key, "ETag") == 0) {
    strncpy(s_etag, e->header_value, sizeof s_etag - 1);
  }
  return ESP_OK;
}

// Returns HTTP status (200/304), or -1.
static int fetch(const char* url, const char* token, bool tls, Body& body) {
  body = {};
  esp_http_client_config_t hc = {};
  hc.url = url;
  hc.timeout_ms = 8000;
  hc.event_handler = on_http;
  hc.user_data = &body;
  hc.buffer_size = 2048;
  if (tls) hc.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t h = esp_http_client_init(&hc);
  if (!h) return -1;
  char auth[80];
  snprintf(auth, sizeof auth, "Bearer %s", token);
  esp_http_client_set_header(h, "Authorization", auth);
  if (s_etag[0]) esp_http_client_set_header(h, "If-None-Match", s_etag);
  esp_err_t e = esp_http_client_perform(h);
  int status = e == ESP_OK ? esp_http_client_get_status_code(h) : -1;
  esp_http_client_cleanup(h);
  if (body.overflow) return -1;
  return status;
}

// Finds C-Ink Settings on the LAN: the provisioned host name first, then any
// _cink._tcp service advertising this device's id.
static bool find_bridge(const Config& c, char* ip, size_t n, uint16_t& port) {
  if (mdns_init() != ESP_OK) return false;
  bool found = false;
  port = c.bridge_port;
  if (c.bridge_host[0]) {
    char host[64];
    strncpy(host, c.bridge_host, sizeof host - 1);
    host[sizeof host - 1] = 0;
    char* dot = strstr(host, ".local");
    if (dot) *dot = 0;
    esp_ip4_addr_t addr = {};
    if (mdns_query_a(host, 2000, &addr) == ESP_OK) {
      snprintf(ip, n, IPSTR, IP2STR(&addr));
      found = true;
    }
  }
  if (!found) {
    mdns_result_t* res = nullptr;
    if (mdns_query_ptr("_cink", "_tcp", 2500, 4, &res) == ESP_OK) {
      for (mdns_result_t* r = res; r && !found; r = r->next) {
        if (!r->addr) continue;
        for (mdns_ip_addr_t* a = r->addr; a; a = a->next) {
          if (a->addr.type != ESP_IPADDR_TYPE_V4) continue;
          snprintf(ip, n, IPSTR, IP2STR(&a->addr.u_addr.ip4));
          port = r->port;
          found = true;
          break;
        }
      }
      mdns_query_results_free(res);
    }
  }
  mdns_free();
  return found;
}

static SyncResult accept(const Config& c, size_t len) {
  int n = digest_open(s_blob, len, c.key, device_id(), s_plain, sizeof s_plain);
  if (n < 0) {
    ESP_LOGW(TAG, "digest did not open with this device's key");
    s_etag[0] = 0;
    return SyncResult::Failed;
  }
  return cache_save(s_blob, len) ? SyncResult::Updated : SyncResult::Failed;
}

SyncResult sync_now(const Config& c, char* ssid_out, char* ip_out, char* source_out) {
  NetInfo ni;
  if (!net_up(c, 8000, ni)) return SyncResult::NoNetwork;
  strcpy(ssid_out, ni.ssid);
  strcpy(ip_out, ni.ip);
  net_time(5000);

  SyncResult result = SyncResult::Failed;
  Body body;
  char url[200];

  char ip[16];
  uint16_t port;
  if (find_bridge(c, ip, sizeof ip, port)) {
    snprintf(url, sizeof url, "http://%s:%u/v1/digest/%s", ip, port, device_id());
    int st = fetch(url, c.token, false, body);
    ESP_LOGI(TAG, "bridge %s -> %d (%u bytes)", ip, st, (unsigned)body.len);
    if (st == 304) result = SyncResult::Unchanged;
    else if (st == 200) result = accept(c, body.len);
    if (result != SyncResult::Failed) strlcpy(source_out, c.bridge_host[0] ? c.bridge_host : ip, 48);
  }
  if (result == SyncResult::Failed && c.relay_url[0]) {
    snprintf(url, sizeof url, "%s/v1/d/%s", c.relay_url, device_id());
    int st = fetch(url, c.token, strncmp(url, "https", 5) == 0, body);
    ESP_LOGI(TAG, "relay -> %d (%u bytes)", st, (unsigned)body.len);
    if (st == 304) result = SyncResult::Unchanged;
    else if (st == 200) result = accept(c, body.len);
    if (result != SyncResult::Failed) {
      const char* host = strstr(c.relay_url, "://");
      strlcpy(source_out, host ? host + 3 : c.relay_url, 48);
    }
  }
  net_down();
  return result;
}

void sync_forget_etag() { s_etag[0] = 0; }
