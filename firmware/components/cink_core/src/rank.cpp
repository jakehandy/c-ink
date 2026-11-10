#include "cink/rank.h"

namespace cink {

static const int64_t MIN = 60, HOUR = 3600;

static int64_t effective_end(const Digest& d, const Card& c) {
  // A commute card is about leaving, so it goes 10 minutes after leave-by
  // even though its "end" (the meeting start) is later.
  if (c.kind == Kind::Commute && c.at) return c.end && c.end < c.at + 10 * MIN ? c.end : c.at + 10 * MIN;
  if (c.end) return c.end;
  if (c.at) return c.at + 15 * MIN;  // an event with no end fades 15 min after start
  return d.horizon;
}

bool card_active(const Digest& d, const Card& c, int64_t now) {
  if (c.start && now < c.start) return false;
  return now < effective_end(d, c);
}

int score_card(const Digest& d, const Card& c, int64_t now) {
  int s = c.priority;
  if (c.at) {
    int64_t dt = c.at - now;
    if (dt <= 0) s += c.kind == Kind::Commute ? 60 : 28;  // happening now (or: you should be on your way)
    else if (dt <= 5 * MIN) s += 60;
    else if (dt <= 15 * MIN) s += 45;
    else if (dt <= 30 * MIN) s += 30;
    else if (dt <= 60 * MIN) s += 18;
    else if (dt <= 3 * HOUR) s += 6;
    else if (dt > 12 * HOUR) s -= 15;
  }
  if (c.urgent) s += 12;
  // Untimed items (a Slack ping, a PR) go stale as the digest ages: the
  // longer since the last sync, the likelier someone already handled it.
  int64_t age = now - d.generated;
  if (!c.at && age > 3 * HOUR) {
    int64_t pen = (age - 3 * HOUR) / (30 * MIN) * 4;
    s -= pen > 20 ? 20 : (int)pen;
  }
  return s;
}

void rank(const Digest& d, int64_t now, uint32_t disabled, Ranked& out) {
  out.n = 0;
  for (int i = 0; i < d.ncards; i++) {
    const Card& c = d.cards[i];
    if (disabled & (1u << (int)c.kind)) continue;
    if (!card_active(d, c, now)) continue;
    int s = score_card(d, c, now);
    // Insertion sort: n <= 24. Ties keep Claude's order.
    int j = out.n++;
    while (j > 0 && out.score[j - 1] < s) {
      out.idx[j] = out.idx[j - 1];
      out.score[j] = out.score[j - 1];
      j--;
    }
    out.idx[j] = (uint8_t)i;
    out.score[j] = (int16_t)s;
  }
}

int64_t next_change(const Digest& d, int64_t now, uint32_t disabled) {
  int64_t best = d.horizon > now ? d.horizon : now + 6 * HOUR;
  auto consider = [&](int64_t t) { if (t > now && t < best) best = t; };
  static const int steps[] = {90, 60, 45, 30, 20, 15, 10, 5, 2, 0};
  for (int i = 0; i < d.ncards; i++) {
    const Card& c = d.cards[i];
    if (disabled & (1u << (int)c.kind)) continue;
    consider(c.start);
    consider(effective_end(d, c));
    if (c.at) {
      for (int s : steps) consider(c.at - s * MIN);
      // Staleness penalties step every 30 minutes after 3 h.
    }
  }
  int64_t age = now - d.generated;
  if (age >= 3 * HOUR) consider(now + (30 * MIN - (age - 3 * HOUR) % (30 * MIN)));
  else consider(d.generated + 3 * HOUR);
  return best;
}

}  // namespace cink
