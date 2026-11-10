// C-Ink simulator: renders a digest exactly as the TICKEY would, to PNG.
//
//   cink_sim fixtures/day.json --now 1762814820 --tz PST8PDT,M3.2.0,M11.1.0 \
//            --screen browse --index 2 --out out.png [--paper] [--mono]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>

#include "cink/canvas.h"
#include "cink/clawd.h"
#include "cink/model.h"
#include "cink/rank.h"
#include "cink/ui.h"
#include "png.h"

using namespace cink;

static Canvas g_cv;
static Digest g_d;

static std::string slurp(const char* p) {
  FILE* f = fopen(p, "rb");
  if (!f) { perror(p); exit(1); }
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}

int main(int argc, char** argv) {
  const char* in = nullptr;
  const char* out = "out.png";
  int64_t now = 0;
  View v = {Screen::Auto, 0};
  Status st;
  memset(&st, 0, sizeof st);
  st.battery = 82;
  st.net = Net::Ok;
  st.quiet_end_hour = 7;
  bool paper = false;
  int64_t sync_age = 120;
  uint32_t disabled = 0;
  bool gallery = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? argv[++i] : (char*)""; };
    if (a == "--now") now = atoll(next());
    else if (a == "--tz") setenv("TZ", next(), 1);
    else if (a == "--out") out = next();
    else if (a == "--index") v.index = atoi(next());
    else if (a == "--battery") st.battery = atoi(next());
    else if (a == "--charging") st.charging = true;
    else if (a == "--offline") st.net = Net::Offline;
    else if (a == "--sync-age") sync_age = atoll(next());
    else if (a == "--mono") st.mono = true;
    else if (a == "--paper") paper = true;
    else if (a == "--disable") disabled |= 1u << (int)kind_from_key(next());
    else if (a == "--screen") {
      std::string s = next();
      if (s == "clawds") gallery = true;
      v.screen = s == "browse" ? Screen::Browse : s == "info" ? Screen::Info : s == "setup" ? Screen::Setup
               : s == "night" ? Screen::Night : Screen::Auto;
    } else in = argv[i];
  }
  tzset();
  std::string json = in ? slurp(in) : "";
  const char* err = nullptr;
  if (in && !parse_digest(json.data(), json.size(), g_d, &err)) {
    fprintf(stderr, "parse failed: %s\n", err);
    return 2;
  }
  if (!now) now = g_d.generated ? g_d.generated + sync_age : (int64_t)time(nullptr);
  st.now = now;
  st.last_sync = g_d.generated ? now - sync_age : 0;
  if (!g_d.generated) st.net = Net::NeverSynced;
  st.next_sync = now + 15 * 60;
  strcpy(st.device_id, "C1A7-22F0");
  strcpy(st.firmware, "0.1.0");
  strcpy(st.ssid, "Handy Home");
  strcpy(st.ip, "192.168.1.48");
  strcpy(st.source, "C-Ink Settings on MacBook");

  int n = 0;
  if (gallery) {
    // Every Clawd pose, for the docs.
    g_cv.clear();
    const int cell = 4, cols = 7, cw = Canvas::W / cols, rh = 100;
    for (int m = 0; m < (int)Mood::Count; m++) {
      int cx = (m % cols) * cw, cy = 4 + (m / cols) * rh;
      clawd(g_cv, cx + (cw - clawd_w(cell)) / 2, cy + 34, cell, (Mood)m, st.mono ? Ink::Black : Ink::Accent);
      const char* name = mood_name((Mood)m);
      g_cv.text(font_caption, cx + (cw - Canvas::measure(font_caption, name)) / 2, cy + 34 + clawd_h(cell) + 22, name,
                Ink::Black);
    }
  } else {
    n = render(g_cv, g_d, st, v, disabled);
  }
  std::vector<uint8_t> px(Canvas::W * Canvas::H);
  for (int y = 0; y < Canvas::H; y++)
    for (int x = 0; x < Canvas::W; x++) px[y * Canvas::W + x] = (uint8_t)g_cv.get(x, y);
  // index 0 white, 1 black, 2 accent. --paper approximates the panel.
  const uint8_t flat[] = {255, 255, 255, 0, 0, 0, 217, 119, 87};  // accent: Claude orange
  const uint8_t real[] = {236, 233, 224, 34, 34, 38, 200, 104, 72};
  if (!png::write(out, Canvas::W, Canvas::H, px.data(), paper ? real : flat, 3)) return 1;
  int64_t nc = next_change(g_d, now, disabled);
  printf("%s: %d ranked cards, frame %08x, next change in %lld s\n", out, n, g_cv.hash(), (long long)(nc - now));
  return 0;
}
