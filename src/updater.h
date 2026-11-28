// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <stddef.h>

void updaterRequestNow();
void updaterBoot();
void updaterStartBackground();
const char* updaterReady();
bool updaterWifiActive();
bool updaterWifiConnected();
bool updaterStatus(char* out, size_t n);
