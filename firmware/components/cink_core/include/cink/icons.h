#pragma once
#include "cink/canvas.h"
#include "cink/model.h"

namespace cink {

// Line icons drawn from primitives so they scale and cost no flash.
void icon(Canvas& c, Kind k, int x, int y, int s, Ink ink);
void icon_battery(Canvas& c, int x, int y, int pct, bool charging, Ink ink);  // 26x12
void icon_wifi(Canvas& c, int x, int y, bool ok, Ink ink);                    // 18x14
void icon_moon(Canvas& c, int x, int y, int s, Ink ink);

}  // namespace cink
