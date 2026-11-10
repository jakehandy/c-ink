/* C API over the renderer, for the desktop app's pixel-exact preview and any
 * other host that wants to draw C-Ink frames. Not thread-safe: callers
 * serialize (the frame and digest buffers are static). */
#ifndef CINK_CAPI_H
#define CINK_CAPI_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CINK_W 720
#define CINK_H 480
#define CINK_PLANE_BYTES (CINK_W / 8 * CINK_H)

typedef struct {
  int64_t now;
  int64_t last_sync;
  int32_t battery;   /* -1 unknown */
  int32_t charging;
  int32_t net;       /* 0 ok, 1 offline, 2 never synced */
  int32_t mono;
  int32_t screen;    /* 0 auto, 1 browse, 2 info, 3 setup, 4 night */
  int32_t index;
  uint32_t disabled; /* bit per kind */
} cink_frame_opts;

/* Renders the digest into two 1-bit planes (MSB first, 90 bytes per row).
 * Returns the number of ranked cards, or -1 if the JSON is not a digest. */
int cink_render_json(const char* json, size_t len, const cink_frame_opts* o, uint8_t* black, uint8_t* accent);

/* Earliest unix time after now when the frame would change. */
int64_t cink_next_change(const char* json, size_t len, int64_t now, uint32_t disabled);

#ifdef __cplusplus
}
#endif
#endif
