// Copyright © Charlie Howard 2026 All rights reserved
#include "launch.h"

static LaunchState state = LAUNCH_IDLE;
static float targetKph = 100.0f;
static uint32_t lastZeroMs = 0;
static uint32_t startMs = 0;
static float prevKph = 0.0f;
static uint32_t prevMs = 0;
static float resultSeconds = 0.0f;

void launchReset(float target) {
  targetKph = target;
  state = LAUNCH_IDLE;
  resultSeconds = 0.0f;
}

void launchSample(float kph, uint32_t ms) {
  switch (state) {
    case LAUNCH_IDLE:
      if (kph < 1.0f) {
        lastZeroMs = ms;
        state = LAUNCH_ARMED;
      }
      break;
    case LAUNCH_ARMED:
      if (kph < 1.0f) {
        lastZeroMs = ms;
      } else {
        startMs = lastZeroMs + (ms - lastZeroMs) / 2;
        prevKph = kph;
        prevMs = ms;
        state = LAUNCH_RUNNING;
      }
      break;
    case LAUNCH_RUNNING:
      if (kph >= targetKph) {
        float span = kph - prevKph;
        float frac = span > 0.01f ? (targetKph - prevKph) / span : 1.0f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        float crossMs = (float)(prevMs - startMs) + frac * (float)(ms - prevMs);
        resultSeconds = crossMs / 1000.0f;
        state = LAUNCH_DONE;
      } else if (kph < 1.0f && ms - startMs > 1000) {
        lastZeroMs = ms;
        state = LAUNCH_ARMED;
      } else if (ms - startMs > 40000) {
        state = LAUNCH_IDLE;
      } else {
        prevKph = kph;
        prevMs = ms;
      }
      break;
    case LAUNCH_DONE:
      break;
  }
}

LaunchState launchState() { return state; }

float launchSeconds(uint32_t nowMs) {
  if (state == LAUNCH_RUNNING) return (float)(nowMs - startMs) / 1000.0f;
  if (state == LAUNCH_DONE) return resultSeconds;
  return 0.0f;
}
