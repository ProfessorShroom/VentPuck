// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <Arduino.h>
#include "dtc.h"

enum ObdState : uint8_t { OBD_STARTING, OBD_SEARCHING, OBD_CONNECTED, OBD_LIVE };

struct LiveData {
  volatile ObdState state = OBD_STARTING;
  volatile float rpm = NAN;
  volatile float speedKph = NAN;
  volatile float coolantC = NAN;
  volatile float oilC = NAN;
  volatile float mapKpa = NAN;
  volatile float baroKpa = NAN;
  volatile float batteryV = NAN;
  volatile uint32_t speedStamp = 0;
};

extern LiveData g_live;

enum Metric : uint8_t { M_BOOST, M_OIL, M_COOLANT, M_RPM, M_SPEED, M_VOLTAGE };

struct WantData {
  volatile uint32_t main = 0x3F;
  volatile uint32_t secondary = 0;
};

extern WantData g_want;

enum DtcCommand : uint8_t { DTC_NONE, DTC_SCAN, DTC_CLEAR };
enum DtcState : uint8_t { DTCS_IDLE, DTCS_SCANNING, DTCS_DONE, DTCS_ERROR, DTCS_CLEARED };

struct DtcStatus {
  volatile DtcCommand command = DTC_NONE;
  volatile DtcState state = DTCS_IDLE;
  volatile int count = 0;
  char codes[DTC_MAX][6] = {{0}};
};

extern DtcStatus g_dtc;

void obdStart();
