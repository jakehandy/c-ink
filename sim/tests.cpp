// Host tests for cink_core. Run: ctest --test-dir build (or ./build/cink_tests)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>

#include "cink/canvas.h"
#include "cink/clawd.h"
#include "cink/model.h"
#include "cink/rank.h"
#include "cink/timefmt.h"
#include "cink/ui.h"

using namespace cink;

static int fails = 0;
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); fails++; } \
  } while (0)

static Digest d;
static Canvas cv;

static bool parse(const std::string& s) { return parse_digest(s.data(), s.size(), d); }

static void test_parse() {
  CHECK(parse(R"({"v":1,"gen":1000,"cards":[{"id":"a","k":"chat","t":"Café 😀 \"hi\"","p":120,
      "r":[{"l":"x","d":"y","tr":7,"u":true}],"sp":[1,2,300],"future":{"nested":[1,{"a":null}]}}]})"));
  CHECK(d.ncards == 1);
  CHECK(d.cards[0].kind == Kind::Chat);
  CHECK(strcmp(d.cards[0].title, "Caf\xC3\xA9 \xF0\x9F\x98\x80 \"hi\"") == 0);
  CHECK(d.cards[0].priority == 100);      // clamped
  CHECK(d.cards[0].rows[0].tier == 2);    // clamped
  CHECK(d.cards[0].spark[2] == 100);
  CHECK(d.horizon == 1000 + 12 * 3600);   // defaulted
  CHECK(!parse(R"({"v":2,"gen":1,"cards":[]})"));
  CHECK(!parse(R"({"v":1,"gen":1,"cards":[{"t":"x"})"));  // truncated
  CHECK(!parse("garbage"));
  // Cards without titles are dropped; extra cards are skipped, not fatal.
  std::string many = R"({"v":1,"gen":5,"cards":[{"k":"event"})";
  for (int i = 0; i < 40; i++) many += R"(,{"t":"c"})";
  many += "]}";
  CHECK(parse(many));
  CHECK(d.ncards == Digest::MAX_CARDS);
}

static void test_truncation_keeps_utf8() {
  std::string t(200, 'a');
  t += "\xC3\xA9\xC3\xA9";  // multibyte right at the cap
  std::string doc = R"({"v":1,"gen":1,"cards":[{"t":")" + t + R"("}]})";
  CHECK(parse(doc));
  size_t n = strlen(d.cards[0].title);
  CHECK(n < sizeof d.cards[0].title);
  const char* p = d.cards[0].title;
  while (next_cp(&p)) {}
  CHECK(p == d.cards[0].title + n);
}

static void test_commute() {
  // Leave-by in 10 min for a meeting in 35. The commute card leads until 10
  // minutes after leave-by, then drops out while the meeting card stays.
  int64_t now = 1'800'000'000;
  char buf[512];
  snprintf(buf, sizeof buf,
           R"({"v":1,"gen":%lld,"cards":[
             {"id":"go","k":"commute","t":"Drive there","p":55,"at":%lld,"en":%lld},
             {"id":"meet","k":"event","t":"Meeting","p":55,"at":%lld}]})",
           (long long)now, (long long)(now + 600), (long long)(now + 2100), (long long)(now + 2100));
  CHECK(parse(buf));
  Ranked r;
  rank(d, now, 0, r);
  CHECK(r.n == 2 && strcmp(d.cards[r.idx[0]].id, "go") == 0);
  rank(d, now + 700, 0, r);  // 100 s past leave-by: still first
  CHECK(r.n == 2 && strcmp(d.cards[r.idx[0]].id, "go") == 0);
  rank(d, now + 1300, 0, r);  // 11 min past leave-by: gone
  CHECK(r.n == 1 && strcmp(d.cards[r.idx[0]].id, "meet") == 0);
}

static void test_rank() {
  int64_t now = 1'800'000'000;
  char buf[1024];
  snprintf(buf, sizeof buf,
           R"({"v":1,"gen":%lld,"cards":[
             {"id":"low","t":"Low","p":30},
             {"id":"meet","k":"event","t":"Meeting","p":40,"at":%lld},
             {"id":"later","k":"event","t":"Later","p":40,"at":%lld},
             {"id":"future","t":"Not yet","p":99,"st":%lld},
             {"id":"gone","t":"Expired","p":99,"en":%lld},
             {"id":"hot","t":"Hot","p":50,"u":true}]})",
           (long long)now, (long long)(now + 4 * 60), (long long)(now + 5 * 3600), (long long)(now + 600),
           (long long)(now - 1));
  CHECK(parse(buf));
  Ranked r;
  rank(d, now, 0, r);
  CHECK(r.n == 4);
  CHECK(strcmp(d.cards[r.idx[0]].id, "meet") == 0);  // 4 min out beats priority
  CHECK(strcmp(d.cards[r.idx[1]].id, "hot") == 0);
  rank(d, now, 1u << (int)Kind::Event, r);
  CHECK(r.n == 2);
  // Ten minutes later the future card is live and the meeting has started.
  rank(d, now + 600, 0, r);
  bool has_future = false;
  for (int i = 0; i < r.n; i++) has_future |= strcmp(d.cards[r.idx[i]].id, "future") == 0;
  CHECK(has_future);
  // Next change: the meeting's 2-minute mark.
  CHECK(next_change(d, now, 0) == now + 2 * 60);
  // Stale untimed cards sink as the digest ages.
  CHECK(score_card(d, d.cards[0], now + 5 * 3600) < score_card(d, d.cards[0], now));
}

static void test_text() {
  CHECK(Canvas::measure(font_body, "") == 0);
  int a = Canvas::measure(font_title, "AV"), b = Canvas::measure(font_title, "A") + Canvas::measure(font_title, "V");
  CHECK(a < b);  // kerning applied
  cv.clear();
  int w = cv.text(font_body, 0, 30, "This sentence is far too long to fit in a hundred pixels", Ink::Black, 0, 100);
  CHECK(w <= 100);
  int lines = cv.wrap(font_body, 0, 30, 200, 30, 2, "one two three four five six seven eight nine ten", Ink::Black, true);
  CHECK(lines == 2);
  lines = cv.wrap(font_body, 0, 30, 400, 30, 3, "short", Ink::Black, true);
  CHECK(lines == 1);
  lines = cv.wrap(font_body, 0, 30, 60, 30, 5, "Supercalifragilistic", Ink::Black, true);
  CHECK(lines >= 2);  // hard break inside a long word
}

static void test_time() {
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
  int64_t t = 1794350820;  // Tue Nov 10 2026 14:47 PST
  char s[48];
  fmt_clock(t, s, sizeof s);
  CHECK(strcmp(s, "2:47 PM") == 0);
  fmt_until(t, t + 12 * 60, s, sizeof s);
  CHECK(strcmp(s, "in 12 min") == 0);
  fmt_until(t, t + 19 * 3600, s, sizeof s);
  CHECK(strcmp(s, "tomorrow 9:47 AM") == 0);
  CHECK(day_diff(t, t + 10 * 3600) == 1);
}

static void test_render_all_screens() {
  CHECK(parse(R"({"v":1,"gen":1794350700,"hl":"h","cards":[
     {"k":"event","t":"Meeting","at":1794351540,"b":"Body text"},
     {"k":"chat","t":"Slack","r":[{"l":"a","d":"b","g":"3"},{"l":"c","tr":0}]},
     {"k":"money","t":"Revenue","big":"$1","sp":[1,5,3]}]})"));
  Status st;
  memset(&st, 0, sizeof st);
  st.now = 1794350820;
  st.battery = 50;
  uint32_t seen[8];
  int n = 0;
  for (int s = 0; s <= 4; s++)
    for (int i = 0; i < (s == 1 ? 2 : 1); i++) {
      View v = {(Screen)s, s == 1 ? i + 1 : 0};  // browse 0 is the auto view
      render(cv, d, st, v, 0);
      seen[n++] = cv.hash();
    }
  // Every screen and every browse position draws something different.
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++) CHECK(seen[i] != seen[j]);
  // Mono mode leaves no accent pixels.
  st.mono = true;
  View v = {Screen::Auto, 0};
  render(cv, d, st, v, 0);
  CHECK(!cv.has_accent());
}

static void test_rotation() {
  // native_byte() reads the portrait planes as the landscape panel scans them.
  static uint8_t plane[Canvas::PLANE];
  memset(plane, 0, sizeof plane);
  auto set = [](int x, int y) { plane[y * Canvas::STRIDE + x / 8] |= 0x80 >> (x & 7); };
  set(0, 0);                             // top-left of the badge
  set(Canvas::W - 1, Canvas::H - 1);     // bottom-right
  // Rotation 0: logical (x, y) lands on native (719 - y, x).
  CHECK(Canvas::native_byte(plane, 0, 89, 0) == 0x01);   // native (719, 0)
  CHECK(Canvas::native_byte(plane, 479, 0, 0) == 0x80);  // native (0, 479)
  // Rotation 1 is the badge turned the other way: (y, 479 - x).
  CHECK(Canvas::native_byte(plane, 479, 0, 1) == 0x80);  // (0,0) -> native (0, 479)
  CHECK(Canvas::native_byte(plane, 0, 89, 1) == 0x01);   // (479,719) -> native (719, 0)
  int ones = 0;
  for (int r = 0; r < 2; r++)
    for (int ny = 0; ny < Canvas::NATIVE_H; ny++)
      for (int bx = 0; bx < Canvas::NATIVE_W / 8; bx++) ones += __builtin_popcount(Canvas::native_byte(plane, ny, bx, r));
  CHECK(ones == 4);
}

static void test_clawd_moods() {
  // Each mood draws a different Clawd, and Clawd stays in its box.
  uint32_t seen[6];
  for (int m = 0; m < 6; m++) {
    cv.clear();
    clawd(cv, 100, 100, 6, (Mood)m, Ink::Accent);
    seen[m] = cv.hash();
    for (int y = 0; y < Canvas::H; y++)
      for (int x = 0; x < Canvas::W; x++)
        if (cv.get(x, y) != Ink::White) {
          CHECK(x >= 100 && x < 100 + clawd_w(6) + 6 * 6);
          CHECK(y >= 100 - 3 * 6 - 4 && y < 100 + clawd_h(6));
        }
  }
  for (int i = 0; i < 6; i++)
    for (int j = i + 1; j < 6; j++) CHECK(seen[i] != seen[j]);
}

int main() {
  test_commute();
  test_rotation();
  test_clawd_moods();
  test_parse();
  test_truncation_keeps_utf8();
  test_rank();
  test_text();
  test_time();
  test_render_all_screens();
  if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
  printf("all tests passed\n");
  return 0;
}
