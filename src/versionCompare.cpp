// Copyright © Charlie Howard 2026 All rights reserved
#include "versionCompare.h"
#include <stdlib.h>
#include <string.h>

bool versionParse(const char* text, FwVersion* out) {
  if (!text || !out) return false;
  if (*text == 'v' || *text == 'V') text++;
  FwVersion v = {{0, 0, 0, 0}, 0};
  const char* p = text;
  for (int i = 0; i < 4; i++) {
    if (*p < '0' || *p > '9') return false;
    char* end = nullptr;
    long n = strtol(p, &end, 10);
    if (n < 0 || n > 100000) return false;
    v.part[i] = (int)n;
    p = end;
    if (i < 3) {
      if (*p != '.') return false;
      p++;
    }
  }
  if (*p == 'b' || *p == 'B') {
    p++;
    if (*p < '0' || *p > '9') return false;
    char* end = nullptr;
    long n = strtol(p, &end, 10);
    if (n < 1 || n > 100000) return false;
    v.beta = (int)n;
    p = end;
  }
  if (*p != '\0') return false;
  *out = v;
  return true;
}

bool versionIsBeta(const FwVersion& v) { return v.beta > 0; }

int versionCompare(const FwVersion& a, const FwVersion& b) {
  for (int i = 0; i < 4; i++) {
    if (a.part[i] != b.part[i]) return a.part[i] < b.part[i] ? -1 : 1;
  }
  if (a.beta == b.beta) return 0;
  if (a.beta == 0) return 1;
  if (b.beta == 0) return -1;
  return a.beta < b.beta ? -1 : 1;
}
