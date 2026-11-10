#pragma once
#include <stdint.h>

#include "cink/model.h"

namespace cink {

struct Ranked {
  uint8_t idx[Digest::MAX_CARDS];
  int16_t score[Digest::MAX_CARDS];
  uint8_t n;
};

// Ranking runs on the device, against the cached digest and the real clock.
// That is what keeps the screen right while offline: Claude ranks once per
// sync, and the device re-ranks every wake as meetings approach and expire.
bool card_active(const Digest& d, const Card& c, int64_t now);
int score_card(const Digest& d, const Card& c, int64_t now);
void rank(const Digest& d, int64_t now, uint32_t disabled, Ranked& out);

// Earliest time after `now` at which the screen would change: a card starting
// or expiring, or the hero countdown crossing a step worth a redraw. The
// firmware sleeps until min(this, next sync).
int64_t next_change(const Digest& d, int64_t now, uint32_t disabled);

}  // namespace cink
