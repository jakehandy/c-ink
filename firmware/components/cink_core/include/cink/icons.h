#pragma once
#include "cink/canvas.h"
#include "cink/model.h"

namespace cink {

// Line icons drawn from primitives so they scale and cost no flash.
void icon(Canvas& c, Kind k, int x, int y, int s, Ink ink);
// The card's source app in monochrome (Slack, GitHub, Gmail...) when there is
// a mark for it at size s (18, 20 or 24), else the kind's line icon.
void card_icon(Canvas& c, const Card& card, int x, int y, int s, Ink ink);
bool brand_icon(Canvas& c, const char* brand, int x, int y, int s, Ink ink);
void wx_icon(Canvas& c, Wx w, int x, int y, int s, Ink ink);
void icon_battery(Canvas& c, int x, int y, int pct, bool charging, Ink ink);  // 26x12
void icon_wifi(Canvas& c, int x, int y, bool ok, Ink ink);                    // 18x14
void icon_moon(Canvas& c, int x, int y, int s, Ink ink);

}  // namespace cink
