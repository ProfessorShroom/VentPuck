// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <Arduino.h>

constexpr int WIFI_SLOTS = 5;

struct Settings {
  bool update = false;
  String updateChannel = "release";  // release | beta
  String wifiSsid[WIFI_SLOTS];
  String wifiPassword[WIFI_SLOTS];
  uint32_t colour1 = 0x1A0650;  // outer edge of the dial gradient
  uint32_t colour2 = 0x3A12A8;  // middle of the dial gradient
  String speedUnit = "mph";  // mph | kph
  String boostUnit = "bar";  // bar | psi | kpa
  String tempUnit = "C";  // C | F
  int brightness = 80;  // percent, 5-100
  bool bootAnimation = true;
  bool darkMode = false;
  String accent = "purple";
  String vehicle = "generic";
  String startScreen = "last";
  String homeItems[4] = {"oil", "coolant", "boost", "rpm"};
};

extern Settings g_settings;
extern bool g_sdOk;  // true if a card was found and mounted
extern uint8_t* g_logoPng;
extern size_t g_logoLen;

void settingsInit();

void settingsSave();
int wifiCount();
