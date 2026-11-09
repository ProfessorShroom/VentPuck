// Copyright © Charlie Howard 2026 All rights reserved

#include <Arduino.h>
#include "display.h"
#include "touch.h"
#include "settings.h"
#include "boot.h"
#include "obd.h"
#include "updater.h"
#include "version.h"

void setup() {
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(1500);  // give USB serial time to come up
  Serial.printf("\nVentPuck %s starting (AMOLED 1.75)\n", FW_VERSION);
  Serial.printf("Chip: %s, PSRAM %u bytes, free heap %u\n", ESP.getChipModel(), (unsigned)ESP.getPsramSize(),
                (unsigned)ESP.getFreeHeap());

  displayInit();  // panel + LVGL, backlight off
  touchInit();
  settingsInit();

  bootShow();

  for (int i = 0; i < 3; i++) {
    displayLoop();
    delay(20);
  }
  displaySetBrightness(g_settings.brightness);
  bootStart();

  uint32_t waitStart = millis();
  while (bootRunning() && millis() - waitStart < 4000) {
    displayLoop();
    delay(5);
  }

  updaterBoot();

  obdStart();
}

void loop() {
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 5000) {
    lastBeat = millis();
    Serial.printf("alive %lus, free heap %u, free PSRAM %u\n", millis() / 1000, (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram());
  }
  displayLoop();
  delay(5);
}
