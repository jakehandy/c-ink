#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cink {

// Minimal pull parser for the digest wire format. No allocation: values are
// copied into caller buffers and anything unexpected is skipped. Malformed
// input sets an error and every later call returns false.
class Json {
 public:
  Json(const char* s, size_t n) : p_(s), end_(s + n) {}
  bool ok() const { return !err_; }

  bool obj_begin();
  bool obj_next(char* key, size_t cap);  // false at '}' or on error
  bool arr_begin();
  bool arr_next();                        // false at ']' or on error
  bool str(char* out, size_t cap);        // truncates on a UTF-8 boundary
  bool i64(int64_t& v);
  bool boolean(bool& v);
  bool null();
  void skip();

 private:
  const char* p_;
  const char* end_;
  bool err_ = false;

  void ws();
  bool lit(const char* s);
  bool fail() { err_ = true; return false; }
};

}  // namespace cink
