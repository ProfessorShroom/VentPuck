// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <Arduino.h>

class SdLog : public Print {
 public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* data, size_t n) override;
};

extern SdLog Log;

void logInit();
void logApply();
