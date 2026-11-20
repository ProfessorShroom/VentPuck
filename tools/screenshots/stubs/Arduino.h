// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
#ifdef __cplusplus
}

#include <string>
#include <algorithm>

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(float v, int dp = 2) { fmt(v, dp); }
  String(double v, int dp = 2) { fmt(v, dp); }
  const char* c_str() const { return s_.c_str(); }
  unsigned length() const { return (unsigned)s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  bool equals(const String& o) const { return s_ == o.s_; }
  bool equalsIgnoreCase(const String& o) const {
    if (s_.size() != o.s_.size()) return false;
    for (size_t i = 0; i < s_.size(); i++)
      if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
    return true;
  }
  bool startsWith(const String& p) const { return s_.rfind(p.s_, 0) == 0; }
  bool endsWith(const String& p) const {
    return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }
  int indexOf(char c, unsigned from = 0) const { auto i = s_.find(c, from); return i == std::string::npos ? -1 : (int)i; }
  int indexOf(const String& t, unsigned from = 0) const { auto i = s_.find(t.s_, from); return i == std::string::npos ? -1 : (int)i; }
  String substring(unsigned from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
  String substring(unsigned from, unsigned to) const {
    if (from > to) std::swap(from, to);
    return from >= s_.size() ? String() : String(s_.substr(from, to - from));
  }
  void toUpperCase() { for (auto& c : s_) c = (char)toupper((unsigned char)c); }
  void toLowerCase() { for (auto& c : s_) c = (char)tolower((unsigned char)c); }
  void trim() {
    size_t a = s_.find_first_not_of(" \t\r\n"), b = s_.find_last_not_of(" \t\r\n");
    s_ = a == std::string::npos ? "" : s_.substr(a, b - a + 1);
  }
  int toInt() const { return atoi(s_.c_str()); }
  float toFloat() const { return (float)atof(s_.c_str()); }
  char operator[](unsigned i) const { return i < s_.size() ? s_[i] : 0; }
  char charAt(unsigned i) const { return (*this)[i]; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += o; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  friend String operator+(String a, const String& b) { a += b; return a; }
  friend String operator+(String a, const char* b) { a += b; return a; }
  friend String operator+(const char* a, const String& b) { return String(a) + b; }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String& o) const { return !(*this == o); }
  bool operator!=(const char* o) const { return !(*this == o); }
  bool operator<(const String& o) const { return s_ < o.s_; }

 private:
  void fmt(double v, int dp) { char b[48]; snprintf(b, sizeof b, "%.*f", dp, v); s_ = b; }
  std::string s_;
};

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* d, size_t n) { size_t k = 0; while (n--) k += write(*d++); return k; }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const String& s) { return print(s.c_str()); }
  size_t print(int v) { return print(String(v)); }
  size_t print(float v, int dp = 2) { return print(String(v, dp)); }
  size_t println() { return print("\n"); }
  template <typename T> size_t println(const T& v) { size_t n = print(v); return n + println(); }
  size_t printf(const char* f, ...) __attribute__((format(printf, 2, 3)));
};
inline size_t Print::printf(const char* f, ...) {
  char b[512];
  va_list a;
  va_start(a, f);
  vsnprintf(b, sizeof b, f, a);
  va_end(a);
  return print(b);
}

inline void delay(uint32_t) {}

struct EspClass {
  void restart() {}
};
static EspClass ESP;

template <typename T> T constrain(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
using std::min;
using std::max;
#endif
