// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <Arduino.h>
#include <map>
#include <string>

class Preferences {
 public:
  static std::map<std::string, float>& store() { static std::map<std::string, float> m; return m; }
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  bool isKey(const char* k) { return store().count(k) > 0; }
  float getFloat(const char* k, float d = 0) { auto i = store().find(k); return i == store().end() ? d : i->second; }
  int getInt(const char* k, int d = 0) { auto i = store().find(k); return i == store().end() ? d : (int)i->second; }
  bool getBool(const char* k, bool d = false) { auto i = store().find(k); return i == store().end() ? d : i->second != 0; }
  uint32_t getUInt(const char* k, uint32_t d = 0) { auto i = store().find(k); return i == store().end() ? d : (uint32_t)i->second; }
  String getString(const char*, const String& d = String()) { return d; }
  size_t putFloat(const char* k, float v) { store()[k] = v; return 4; }
  size_t putInt(const char* k, int v) { store()[k] = (float)v; return 4; }
  size_t putBool(const char* k, bool v) { store()[k] = v; return 1; }
  size_t putUInt(const char* k, uint32_t v) { store()[k] = (float)v; return 4; }
  size_t putString(const char*, const String& v) { return v.length(); }
  bool remove(const char* k) { return store().erase(k) > 0; }
  bool clear() { store().clear(); return true; }
};
