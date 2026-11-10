// C-Ink firmware: wake, maybe sync, draw the most relevant thing, sleep.
//
// The device spends nearly all of its life in deep sleep. It wakes for one of
// three reasons: the sync timer (default every 15 minutes), the screen being
// due to change (a meeting getting close, a card expiring), or a button.
// Ranking happens here, against the cached digest and the real clock, so the
// screen stays correct for hours with no network at all.
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "buffers.h"
#include "cache.h"
#include "cink/canvas.h"
#include "cink/model.h"
#include "cink/rank.h"
#include "cink/timefmt.h"
#include "cink/ui.h"
#include "cink_hal/board.h"
#include "cink_hal/hal.h"
#include "config.h"
#include "crypto.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sync.h"
#include "usb_link.h"

using namespace cink;

static const char* TAG = "cink";
static const uint32_t kRtcMagic = 0xC1A70001;
static const int kIdleMs = 20000;     // interactive session timeout
static const int kLongPressMs = 1500;

struct RtcState {
  uint32_t magic;
  uint32_t shown_hash;
  uint32_t shown_accent;
  int64_t last_sync;
  int64_t next_sync;
  uint16_t fails;
  uint16_t fast_since_full;
  char ssid[33];
  char ip[16];
  char source[48];
};
RTC_DATA_ATTR static RtcState R;

static Canvas g_cv;
static Digest g_digest;
static Config g_cfg;
static bool g_have_digest;
static View g_view = {Screen::Auto, 0};

static int64_t now_s() { return (int64_t)time(nullptr); }
static int64_t ms() { return esp_timer_get_time() / 1000; }

static void apply_tz() {
  setenv("TZ", g_cfg.tz[0] ? g_cfg.tz : "UTC0", 1);
  tzset();
}

static void load_digest() {
  g_have_digest = false;
  memset(&g_digest, 0, sizeof g_digest);
  size_t n = cache_load(g_blob_buf, sizeof g_blob_buf);
  if (!n) { sync_forget_etag(); return; }
  char* plain = nullptr;
  int m = digest_open_inplace(g_blob_buf, n, g_cfg.key, device_id(), &plain);
  if (m < 0) { sync_forget_etag(); return; }
  const char* err = nullptr;
  g_have_digest = parse_digest(plain, (size_t)m, g_digest, &err);
  if (!g_have_digest) ESP_LOGW(TAG, "cached digest rejected: %s", err ? err : "?");
  memset(g_blob_buf, 0, n);  // plaintext only lives in the parsed struct
  // The bridge keeps the same digest (and ETag) while nothing changed, so a
  // 304 is a fresh confirmation: age untimed cards from the last good sync.
  if (g_have_digest && R.last_sync > g_digest.generated) g_digest.generated = R.last_sync;
  if (g_have_digest && g_digest.cfg.present) {
    // Settings changed in C-Ink Settings arrive with the digest.
    const DeviceSettings& d = g_digest.cfg;
    Config next = g_cfg;
    if (d.refresh_min) next.refresh_min = (uint16_t)d.refresh_min;
    next.disabled = d.disabled;
    next.quiet_start = d.quiet_start;
    next.quiet_end = d.quiet_end;
    next.mono = d.mono;
    next.rotation = d.rotation;
    if (d.tz[0]) strncpy(next.tz, d.tz, sizeof next.tz - 1);
    if (memcmp(&next, &g_cfg, sizeof next) != 0) {
      ESP_LOGI(TAG, "applying settings from digest");
      g_cfg = next;
      config_save(g_cfg);
      apply_tz();
    }
  }
}

static bool in_quiet(int64_t t) {
  if (g_cfg.quiet_start < 0 || g_cfg.quiet_end < 0 || g_cfg.quiet_start == g_cfg.quiet_end) return false;
  int h = local(t).hour;
  if (g_cfg.quiet_start < g_cfg.quiet_end) return h >= g_cfg.quiet_start && h < g_cfg.quiet_end;
  return h >= g_cfg.quiet_start || h < g_cfg.quiet_end;
}

static int64_t quiet_ends(int64_t t) {
  int64_t e = local_midnight(t) + g_cfg.quiet_end * 3600;
  while (e <= t) e += 86400;
  return e;
}

static Status make_status() {
  Status st;
  memset(&st, 0, sizeof st);
  st.now = now_s();
  st.last_sync = R.last_sync ? R.last_sync : (g_have_digest ? g_digest.generated : 0);
  st.next_sync = R.next_sync;
  st.battery = hal::battery_percent();
  st.charging = hal::charging();
  if (!st.last_sync) st.net = Net::NeverSynced;
  else if (R.fails > 0 || !R.last_sync) st.net = Net::Offline;
  else st.net = Net::Ok;
  st.mono = g_cfg.mono;
  strncpy(st.device_id, device_id(), sizeof st.device_id - 1);
  strncpy(st.firmware, esp_app_get_description()->version, sizeof st.firmware - 1);
  strncpy(st.ssid, R.ssid, sizeof st.ssid - 1);
  strncpy(st.ip, R.ip, sizeof st.ip - 1);
  strncpy(st.source, R.source, sizeof st.source - 1);
  st.quiet_end_hour = g_cfg.quiet_end;
  return st;
}

static uint32_t accent_hash() {
  uint32_t h = 2166136261u;
  for (int i = 0; i < Canvas::PLANE; i++) { h ^= g_cv.accent[i]; h *= 16777619u; }
  return h;
}

// Renders `v` and pushes it to the panel only if the pixels changed. Fast
// mono refreshes by default; a full refresh when the accent color changed or
// enough fast ones have piled up ghosting.
static int show(View v, bool force_full = false) {
  if (!g_cfg.provisioned) v.screen = Screen::Setup;
  else if (v.screen == Screen::Auto && in_quiet(now_s())) v.screen = Screen::Night;
  Status st = make_status();
  int n = render(g_cv, g_digest, st, v, g_cfg.disabled);
  uint32_t h = g_cv.hash(), ah = accent_hash();
  if (h == R.shown_hash && !force_full) return n;
  bool accent_changed = !g_cfg.mono && ah != R.shown_accent && g_cv.has_accent();
  hal::Refresh mode = (force_full || accent_changed || R.fast_since_full >= CONFIG_CINK_FULL_REFRESH_EVERY)
                          ? hal::Refresh::Full
                          : hal::Refresh::Fast;
  hal::display_present(g_cv.black, g_cv.accent, mode, g_cfg.rotation);
  R.shown_hash = h;
  R.shown_accent = ah;
  R.fast_since_full = mode == hal::Refresh::Full ? 0 : R.fast_since_full + 1;
  return n;
}

static void schedule_after_sync(bool ok) {
  int64_t interval = (int64_t)g_cfg.refresh_min * 60;
  if (!ok) {
    // Back off while offline: 1x, 2x, 4x, 8x the interval, at most 2 hours.
    int shift = R.fails > 4 ? 3 : R.fails - 1;
    interval <<= shift;
    if (interval > 7200) interval = 7200;
  }
  R.next_sync = now_s() + interval;
}

static void do_sync() {
  if (!g_cfg.provisioned) return;
  SyncResult r = sync_now(g_cfg, R.ssid, R.ip, R.source);
  ESP_LOGI(TAG, "sync: %d", (int)r);
  bool ok = r == SyncResult::Updated || r == SyncResult::Unchanged;
  if (r == SyncResult::Updated) load_digest();
  if (ok) {
    R.last_sync = now_s();
    if (g_have_digest && r == SyncResult::Unchanged) g_digest.generated = R.last_sync;
    R.fails = 0;
  } else if (R.fails < 1000) {
    R.fails++;
  }
  schedule_after_sync(ok);
}

// ------------------------------------------------------------- navigation

static void press(uint8_t button, bool long_press) {
  if (button == hal::BTN_BACK && long_press) {
    do_sync();
    g_view = {Screen::Auto, 0};
  } else if (button == hal::BTN_UP) {
    g_view.index = g_view.screen == Screen::Browse ? g_view.index - 1 : -1;
    g_view.screen = Screen::Browse;
  } else if (button == hal::BTN_DOWN) {
    g_view.index = g_view.screen == Screen::Browse ? g_view.index + 1 : 1;
    g_view.screen = Screen::Browse;
  } else if (button == hal::BTN_BACK) {
    g_view = g_view.screen == Screen::Auto ? View{Screen::Info, 0} : View{Screen::Auto, 0};
  } else if (button == (hal::BTN_UP | hal::BTN_DOWN)) {
    g_view = {Screen::Info, 0};
  }
  show(g_view);
}

// With the badge flipped 180 degrees the physical Up button sits at the bottom,
// so it pages down. USB presses from C-Ink Settings are logical and skip this.
static uint8_t oriented(uint8_t b) {
  const uint8_t ud = hal::BTN_UP | hal::BTN_DOWN;
  if (g_cfg.rotation && (b & ud) && (b & ud) != ud) b ^= ud;
  return b;
}

// Polls the buttons; returns true if something was pressed.
static bool poll_buttons() {
  static uint8_t held;
  static int64_t since;
  static bool fired;
  uint8_t b = oriented(hal::buttons_read());
  if (b && !held) { held = b; since = ms(); fired = false; return false; }
  if (b && held) {
    held |= b;
    if (!fired && held == hal::BTN_BACK && ms() - since >= kLongPressMs) {
      fired = true;
      press(hal::BTN_BACK, true);
      return true;
    }
    return false;
  }
  if (!b && held) {
    uint8_t h = held;
    held = 0;
    if (fired || ms() - since < 30) return false;  // long press already handled, or bounce
    press(h, false);
    return true;
  }
  return false;
}

static void interactive(uint8_t first) {
  if (first) press(first, false);
  int64_t deadline = ms() + kIdleMs;
  while (ms() < deadline) {
    if (poll_buttons()) deadline = ms() + kIdleMs;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  g_view = {Screen::Auto, 0};
  show(g_view);
}

// ------------------------------------------------------------ tethered mode

static void tethered() {
  ESP_LOGI(TAG, "USB host present: staying awake for C-Ink Settings");
  usb_init(g_cv.black, g_cv.accent);
  int64_t last_input = ms(), unplugged_since = 0, next_tick = 0;
  for (;;) {
    UsbStatus us = {hal::battery_percent(), hal::charging(), R.ssid, R.ip, R.last_sync,
                    g_have_digest ? g_digest.ncards : 0, esp_app_get_description()->version};
    switch (usb_poll(g_cfg, us)) {
      case UsbEvent::ConfigChanged:
        apply_tz();
        load_digest();
        R.next_sync = 0;  // sync with the new settings right away
        R.shown_hash = 0;
        show(g_view);
        break;
      case UsbEvent::DigestPushed:
        load_digest();
        R.last_sync = now_s();
        R.fails = 0;
        show(g_view = {Screen::Auto, 0});
        break;
      case UsbEvent::SyncNow: do_sync(); show(g_view); break;
      case UsbEvent::PressUp: press(hal::BTN_UP, false); last_input = ms(); break;
      case UsbEvent::PressDown: press(hal::BTN_DOWN, false); last_input = ms(); break;
      case UsbEvent::PressBack: press(hal::BTN_BACK, false); last_input = ms(); break;
      case UsbEvent::ShowInfo: press(hal::BTN_UP | hal::BTN_DOWN, false); last_input = ms(); break;
      case UsbEvent::Reset: vTaskDelay(pdMS_TO_TICKS(200)); esp_restart(); break;
      case UsbEvent::BoardChanged:
        hal::buttons_init();
        hal::display_init();
        R.shown_hash = 0;
        show(g_view, true);
        break;
      case UsbEvent::PanelTest:
        R.shown_hash = 0;
        show(g_view, true);
        usb_panel_report();
        break;
      case UsbEvent::None: break;
    }
    if (poll_buttons()) last_input = ms();
    if (ms() >= next_tick) {
      next_tick = ms() + 5000;
      if (g_cfg.provisioned && now_s() >= R.next_sync && !in_quiet(now_s())) do_sync();
      if (g_view.screen != Screen::Auto && ms() - last_input > kIdleMs) g_view = {Screen::Auto, 0};
      show(g_view);  // no-op unless the frame changed (countdowns, expiries)
    }
    if (!hal::usb_connected()) {
      if (!unplugged_since) unplugged_since = ms();
      if (ms() - unplugged_since > 3000) return;
    } else {
      unplugged_since = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ------------------------------------------------------------------ sleep

[[noreturn]] static void sleep_now() {
  int64_t now = now_s();
  int64_t wake = 0;
  if (g_cfg.provisioned) {
    if (in_quiet(now)) {
      wake = quiet_ends(now);
      if (R.next_sync < wake) R.next_sync = wake;  // first thing after quiet hours: sync
    } else {
      wake = R.next_sync;
      if (g_have_digest) {
        int64_t nc = next_change(g_digest, now, g_cfg.disabled);
        if (nc < wake) wake = nc;
      }
      // A night screen should appear on time too.
      if (g_cfg.quiet_start >= 0) {
        int64_t qs = local_midnight(now) + g_cfg.quiet_start * 3600;
        while (qs <= now) qs += 86400;
        if (qs < wake) wake = qs;
      }
    }
    int64_t delay = wake - now;
    if (delay < 30) delay = 30;
    ESP_LOGI(TAG, "sleeping %lld s", (long long)delay);
    esp_sleep_enable_timer_wakeup((uint64_t)delay * 1000000ULL);
  }
  // Not provisioned: no timer. A button (or plugging in) wakes it.
  hal::buttons_prepare_sleep();
  hal::display_sleep();
  esp_deep_sleep_start();
}

extern "C" void app_main() {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  if (R.magic != kRtcMagic) {
    memset(&R, 0, sizeof R);
    R.magic = kRtcMagic;
  }
  config_load(g_cfg);
  apply_tz();
  hal::board_init();  // pins and panel from the stored board profile
  hal::power_init();
  hal::buttons_init();
  hal::display_init();
  load_digest();

  esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  ESP_LOGI(TAG, "C-Ink %s on %s, wake %d, provisioned %d, digest %d, free heap %u", esp_app_get_description()->version,
           device_id(), (int)why, g_cfg.provisioned, g_have_digest, (unsigned)esp_get_free_heap_size());

  if (why == ESP_SLEEP_WAKEUP_EXT1) {
    uint8_t b = oriented(hal::buttons_from_wake_mask(esp_sleep_get_ext1_wakeup_status()));
    interactive(b);
  }
  if (g_cfg.provisioned && now_s() >= R.next_sync && !in_quiet(now_s())) do_sync();
  g_view = {Screen::Auto, 0};
  show(g_view);
  if (hal::usb_connected()) tethered();
  sleep_now();
}
