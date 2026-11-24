// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <Arduino.h>

constexpr int WIFI_SLOTS = 5;

struct Settings {
  bool update = false;
  String updateChannel = "release";  // release | beta
  String wifiSsid[WIFI_SLOTS];
  String wifiPassword[WIFI_SLOTS];
  uint32_t colour1 = 0x1A0650;  // outer
  uint32_t colour2 = 0x3A12A8;  // middle
  String speedUnit = "mph";  // mph | kph
  String boostUnit = "bar";  // bar | psi | kpa
  String tempUnit = "C";  // C | F
  int brightness = 80;  // 5-100
  bool flipScreen = false;  // rotated 180 degrees, for mounting upside down
  bool bootAnimation = true;
  bool bootSound = true;
  bool darkMode = false;
  bool sdLog = false;
  String accent = "purple";
  String vehicle = "generic";
  String adapterName = "OBDLink";  // part of the adapter's Bluetooth name, or empty for any OBD adapter
  String startScreen = "last";
  String homeItems[4] = {"oil", "coolant", "boost", "rpm"};
};

extern Settings g_settings;
extern bool g_sdOk;
extern uint8_t* g_logoPng;
extern size_t g_logoLen;

void settingsInit();

void settingsSave();
int wifiCount();
bool vehicleHasBoost();
