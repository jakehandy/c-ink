#include "cink/json.h"

#include <string.h>

namespace cink {

void Json::ws() {
  while (p_ < end_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\r' || *p_ == '\t')) p_++;
}

bool Json::lit(const char* s) {
  size_t n = strlen(s);
  if ((size_t)(end_ - p_) < n || memcmp(p_, s, n) != 0) return false;
  p_ += n;
  return true;
}

// Separators are handled leniently: a comma before the next member is
// consumed if present. The bridge produces strict JSON; leniency here only
// means a hand-edited cache file still loads.
bool Json::obj_begin() {
  if (err_) return false;
  ws();
  if (p_ >= end_ || *p_ != '{') return fail();
  p_++;
  return true;
}

bool Json::obj_next(char* key, size_t cap) {
  if (err_) return false;
  ws();
  if (p_ < end_ && *p_ == ',') { p_++; ws(); }
  if (p_ >= end_) return fail();
  if (*p_ == '}') { p_++; return false; }
  if (!str(key, cap)) return fail();
  ws();
  if (p_ >= end_ || *p_ != ':') return fail();
  p_++;
  return true;
}

bool Json::arr_begin() {
  if (err_) return false;
  ws();
  if (p_ >= end_ || *p_ != '[') return fail();
  p_++;
  return true;
}

bool Json::arr_next() {
  if (err_) return false;
  ws();
  if (p_ < end_ && *p_ == ',') { p_++; ws(); }
  if (p_ >= end_) return fail();
  if (*p_ == ']') { p_++; return false; }
  return true;
}

static size_t put_utf8(char* o, uint32_t cp) {
  if (cp < 0x80) { o[0] = (char)cp; return 1; }
  if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) {
    o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

static int hex4(const char* s, uint32_t& v) {
  v = 0;
  for (int i = 0; i < 4; i++) {
    char c = s[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    else return 0;
  }
  return 1;
}

bool Json::str(char* out, size_t cap) {
  if (err_) return false;
  ws();
  if (p_ >= end_ || *p_ != '"') return fail();
  p_++;
  size_t n = 0;
  bool full = false;
  while (p_ < end_ && *p_ != '"') {
    char tmp[4];
    size_t len = 0;
    if (*p_ == '\\') {
      p_++;
      if (p_ >= end_) return fail();
      char e = *p_++;
      switch (e) {
        case 'n': tmp[0] = '\n'; len = 1; break;
        case 't': tmp[0] = ' '; len = 1; break;
        case 'r': tmp[0] = ' '; len = 1; break;
        case 'b': case 'f': len = 0; break;
        case 'u': {
          uint32_t cp;
          if (end_ - p_ < 4 || !hex4(p_, cp)) return fail();
          p_ += 4;
          if (cp >= 0xD800 && cp < 0xDC00 && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
            uint32_t lo;
            if (hex4(p_ + 2, lo) && lo >= 0xDC00 && lo < 0xE000) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              p_ += 6;
            }
          }
          len = put_utf8(tmp, cp);
          break;
        }
        default: tmp[0] = e; len = 1; break;
      }
    } else {
      // Copy one whole UTF-8 sequence so truncation never splits it.
      unsigned char c = (unsigned char)*p_;
      len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
      if ((size_t)(end_ - p_) < len) return fail();
      memcpy(tmp, p_, len);
      p_ += len;
    }
    if (!full && out && n + len < cap) {
      memcpy(out + n, tmp, len);
      n += len;
    } else {
      full = true;
    }
  }
  if (p_ >= end_) return fail();
  p_++;
  if (out && cap) out[n] = 0;
  return true;
}

bool Json::i64(int64_t& v) {
  if (err_) return false;
  ws();
  bool neg = false;
  if (p_ < end_ && *p_ == '-') { neg = true; p_++; }
  if (p_ >= end_ || *p_ < '0' || *p_ > '9') return fail();
  int64_t r = 0;
  while (p_ < end_ && *p_ >= '0' && *p_ <= '9') r = r * 10 + (*p_++ - '0');
  // Accept and drop a fractional part or exponent.
  if (p_ < end_ && *p_ == '.') { p_++; while (p_ < end_ && *p_ >= '0' && *p_ <= '9') p_++; }
  if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
    p_++;
    if (p_ < end_ && (*p_ == '+' || *p_ == '-')) p_++;
    while (p_ < end_ && *p_ >= '0' && *p_ <= '9') p_++;
  }
  v = neg ? -r : r;
  return true;
}

bool Json::boolean(bool& v) {
  if (err_) return false;
  ws();
  if (lit("true")) { v = true; return true; }
  if (lit("false")) { v = false; return true; }
  return fail();
}

bool Json::null() {
  if (err_) return false;
  ws();
  return lit("null");
}

void Json::skip() {
  if (err_) return;
  ws();
  if (p_ >= end_) { fail(); return; }
  char c = *p_;
  if (c == '"') { str(nullptr, 0); return; }
  if (c == '{') {
    obj_begin();
    char k[2];
    while (obj_next(k, sizeof k)) skip();
    return;
  }
  if (c == '[') {
    arr_begin();
    while (arr_next()) skip();
    return;
  }
  if (c == 't' || c == 'f') { bool b; boolean(b); return; }
  if (c == 'n') { if (!null()) fail(); return; }
  int64_t v;
  i64(v);
}

}  // namespace cink
