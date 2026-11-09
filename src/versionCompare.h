// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <stdbool.h>

struct FwVersion {
  int part[4];
  int beta;
};

bool versionParse(const char* text, FwVersion* out);
int versionCompare(const FwVersion& a, const FwVersion& b);
bool versionIsBeta(const FwVersion& v);
