#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cink {

// All formatting is in the process's local timezone (TZ). The device sets TZ
// from the config the desktop app provisions; the simulator from the env.
struct LocalTime { int year, mon, mday, wday, hour, min; };
LocalTime local(int64_t t);
int64_t local_midnight(int64_t t);       // start of t's local day
int day_diff(int64_t from, int64_t to);  // calendar days between local dates

void fmt_clock(int64_t t, char* out, size_t n);           // "2:47 PM"
void fmt_day(int64_t t, char* out, size_t n);             // "TUE NOV 10"
void fmt_until(int64_t now, int64_t at, char* out, size_t n);  // "in 12 min", "tomorrow 9:30 AM"
void fmt_ago(int64_t now, int64_t t, char* out, size_t n);     // "3 min ago"
void fmt_when(int64_t now, int64_t at, char* out, size_t n);   // "3:30 PM", "TOMORROW", "THU"

}  // namespace cink
