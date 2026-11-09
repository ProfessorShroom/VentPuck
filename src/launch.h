// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <stdint.h>

enum LaunchState : uint8_t { LAUNCH_IDLE, LAUNCH_ARMED, LAUNCH_RUNNING, LAUNCH_DONE };

void launchReset(float targetKph);
void launchSample(float kph, uint32_t ms);
LaunchState launchState();
float launchSeconds(uint32_t nowMs);
