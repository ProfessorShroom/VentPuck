// Copyright © Charlie Howard 2026 All rights reserved
#pragma once

#include <stdint.h>

// Plays a short, soft five-note run through the ES8311 codec and speaker. Returns straight away, the sound plays on its own task.
// delayMs holds it back so it can land on a moment in the boot animation.
// Call after touchInit(), which brings up the shared I2C bus.
void soundPlayBoot(uint32_t delayMs = 0);
