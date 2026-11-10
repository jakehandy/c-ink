#pragma once
#include <stdint.h>

#include "cink/canvas.h"
#include "cink/model.h"
#include "cink/rank.h"

namespace cink {

enum class Screen : uint8_t {
  Auto,    // the most relevant card right now
  Browse,  // user is paging with Up/Down; index selects the card
  Info,    // device details
  Setup,   // not provisioned yet
  Night,   // quiet hours
};

enum class Net : uint8_t { Ok, Offline, NeverSynced };

struct Status {
  int64_t now;
  int64_t last_sync;   // last successful sync (0 = never)
  int64_t next_sync;   // planned
  int battery;         // percent, -1 unknown
  bool charging;
  Net net;
  bool mono;           // render accent as black (mono-only panel mode)
  char device_id[20];
  char firmware[16];
  char ssid[33];
  char ip[16];
  char source[48];     // where digests come from: "Jake's MacBook" or relay host
  int quiet_end_hour;  // for the night screen, -1 if unset
};

struct View {
  Screen screen;
  int index;  // Browse: position in the ranked list
};

// Renders one full frame. Returns the number of ranked cards (for the
// firmware's Up/Down wraparound).
int render(Canvas& cv, const Digest& d, const Status& st, const View& v, uint32_t disabled);

}  // namespace cink
