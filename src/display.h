// Copyright © Charlie Howard 2026 All rights reserved
#pragma once

// Waveshare ESP32-S3-Touch-AMOLED-1.75: CO5300 QSPI AMOLED, 466x466.
void displayInit();                   // starts the panel (dark) and LVGL
void displaySetBrightness(int percent);  // 0-100
void displayLoop();                   // call from loop(): runs LVGL
