// Copyright © Charlie Howard 2026 All rights reserved
#include "dtc.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static void addCode(DtcList* out, unsigned char a, unsigned char b) {
  if (a == 0 && b == 0) return;
  char code[6];
  const char letters[] = {'P', 'C', 'B', 'U'};
  snprintf(code, sizeof code, "%c%d%X%X%X", letters[a >> 6], (a >> 4) & 3, a & 0xF, b >> 4, b & 0xF);
  for (int i = 0; i < out->count; i++) {
    if (strcmp(out->codes[i], code) == 0) return;
  }
  if (out->count < DTC_MAX) {
    strcpy(out->codes[out->count], code);
    out->count++;
  }
}

static void processMessage(const char* hex, DtcList* out) {
  unsigned char bytes[64];
  int n = 0;
  for (int i = 0; hex[i] && hex[i + 1] && n < 64; i += 2) {
    int hi = hexVal(hex[i]);
    int lo = hexVal(hex[i + 1]);
    if (hi < 0 || lo < 0) return;
    bytes[n++] = (unsigned char)(hi * 16 + lo);
  }
  if (n < 2 || bytes[0] != 0x43) return;
  int start = 1;
  int end = n;
  int declared = bytes[1];
  if (n >= 2 + declared * 2) {
    start = 2;
    end = 2 + declared * 2;
  }
  for (int i = start; i + 1 < end; i += 2) addCode(out, bytes[i], bytes[i + 1]);
}

static bool allHex(const char* s, int len) {
  for (int i = 0; i < len; i++) {
    if (hexVal(s[i]) < 0) return false;
  }
  return len > 0;
}

void dtcParse(const char* reply, DtcList* out) {
  out->count = 0;
  char pending[160];
  pending[0] = 0;
  bool inMulti = false;
  const char* p = reply;
  while (*p) {
    char line[96];
    int len = 0;
    while (*p && *p != '|' && len < 95) line[len++] = *p++;
    while (*p && *p != '|') p++;
    if (*p == '|') p++;
    line[len] = 0;
    char* s = line;
    while (*s == ' ') s++;
    int sl = (int)strlen(s);
    while (sl > 0 && s[sl - 1] == ' ') s[--sl] = 0;
    if (sl == 0) continue;
    char* colon = strchr(s, ':');
    if (colon && colon - s <= 2 && allHex(s, (int)(colon - s))) {
      if (strlen(pending) + strlen(colon + 1) < sizeof pending) strcat(pending, colon + 1);
      inMulti = true;
      continue;
    }
    if (inMulti) {
      processMessage(pending, out);
      pending[0] = 0;
      inMulti = false;
    }
    if (sl == 3 && allHex(s, 3)) {
      inMulti = true;
      continue;
    }
    if (sl >= 4 && (sl % 2) == 0 && allHex(s, sl)) processMessage(s, out);
  }
  if (inMulti) processMessage(pending, out);
}
