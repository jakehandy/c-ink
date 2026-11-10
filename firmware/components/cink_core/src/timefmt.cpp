#include "cink/timefmt.h"

#include <stdio.h>
#include <time.h>

namespace cink {

static void to_tm(int64_t t, struct tm* out) {
  time_t tt = (time_t)t;
#if defined(_WIN32)
  localtime_s(out, &tt);
#else
  localtime_r(&tt, out);
#endif
}

LocalTime local(int64_t t) {
  struct tm tm;
  to_tm(t, &tm);
  return {tm.tm_year + 1900, tm.tm_mon, tm.tm_mday, tm.tm_wday, tm.tm_hour, tm.tm_min};
}

int64_t local_midnight(int64_t t) {
  struct tm tm;
  to_tm(t, &tm);
  tm.tm_hour = 0;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return (int64_t)mktime(&tm);
}

int day_diff(int64_t from, int64_t to) {
  // Midnight-to-midnight, rounded, so DST days count as one.
  int64_t a = local_midnight(from), b = local_midnight(to);
  int64_t d = b - a;
  return (int)((d + (d >= 0 ? 43200 : -43200)) / 86400);
}

static const char* const kDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* const kDaysTitle[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

void fmt_clock(int64_t t, char* out, size_t n) {
  LocalTime l = local(t);
  int h = l.hour % 12;
  if (h == 0) h = 12;
  snprintf(out, n, "%d:%02d %s", h, l.min, l.hour < 12 ? "AM" : "PM");
}

void fmt_day(int64_t t, char* out, size_t n) {
  LocalTime l = local(t);
  snprintf(out, n, "%s  %s %d", kDays[l.wday], kMonths[l.mon], l.mday);
}

void fmt_until(int64_t now, int64_t at, char* out, size_t n) {
  int64_t d = at - now;
  char clock[16];
  fmt_clock(at, clock, sizeof clock);
  if (d <= 60) { snprintf(out, n, "now"); return; }
  int days = day_diff(now, at);
  if (days == 0) {
    int m = (int)((d + 59) / 60);
    if (m < 60) snprintf(out, n, "in %d min", m);
    else if (m < 180 && m % 60) snprintf(out, n, "in %d h %d min", m / 60, m % 60);
    else if (m < 180) snprintf(out, n, "in %d h", m / 60);
    else snprintf(out, n, "at %s", clock);
    return;
  }
  if (days == 1) { snprintf(out, n, "tomorrow %s", clock); return; }
  LocalTime l = local(at);
  snprintf(out, n, "%s %s", kDaysTitle[l.wday], clock);
}

void fmt_ago(int64_t now, int64_t t, char* out, size_t n) {
  int64_t d = now - t;
  if (d < 90) snprintf(out, n, "just now");
  else if (d < 3600) snprintf(out, n, "%d min ago", (int)(d / 60));
  else if (d < 86400) snprintf(out, n, "%d h ago", (int)(d / 3600));
  else if (d < 2 * 86400) snprintf(out, n, "yesterday");
  else snprintf(out, n, "%d days ago", (int)(d / 86400));
}

void fmt_when(int64_t now, int64_t at, char* out, size_t n) {
  int days = day_diff(now, at);
  if (days == 0) { fmt_clock(at, out, n); return; }
  if (days == 1) { snprintf(out, n, "TOMORROW"); return; }
  LocalTime l = local(at);
  if (days > 1 && days < 7) { snprintf(out, n, "%s", kDays[l.wday]); return; }
  snprintf(out, n, "%s %d", kMonths[l.mon], l.mday);
}

}  // namespace cink
