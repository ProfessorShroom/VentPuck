// Copyright © Charlie Howard 2026 All rights reserved
#pragma once

const int DTC_MAX = 12;

struct DtcList {
  int count;
  char codes[DTC_MAX][6];
};

void dtcParse(const char* reply, DtcList* out);
