#include "cink/cink.h"

#include <string.h>

#include "cink/canvas.h"
#include "cink/model.h"
#include "cink/rank.h"
#include "cink/ui.h"

using namespace cink;

static Digest g_digest;
static Canvas g_canvas;

extern "C" int cink_render_json(const char* json, size_t len, const cink_frame_opts* o, uint8_t* black,
                                uint8_t* accent) {
  Status st;
  memset(&st, 0, sizeof st);
  st.now = o->now;
  st.last_sync = o->last_sync;
  st.battery = o->battery;
  st.charging = o->charging != 0;
  st.net = (Net)o->net;
  st.mono = o->mono != 0;
  st.quiet_end_hour = 7;
  strcpy(st.device_id, "PREVIEW");
  strcpy(st.firmware, "preview");
  View v = {(Screen)o->screen, o->index};
  bool ok = parse_digest(json, len, g_digest);
  if (!ok) {
    if (v.screen != Screen::Setup) return -1;
    memset(&g_digest, 0, sizeof g_digest);
  }
  int n = render(g_canvas, g_digest, st, v, o->disabled);
  if (black) memcpy(black, g_canvas.black, Canvas::PLANE);
  if (accent) memcpy(accent, g_canvas.accent, Canvas::PLANE);
  return n;
}

extern "C" int64_t cink_next_change(const char* json, size_t len, int64_t now, uint32_t disabled) {
  if (!parse_digest(json, len, g_digest)) return now + 15 * 60;
  return next_change(g_digest, now, disabled);
}
