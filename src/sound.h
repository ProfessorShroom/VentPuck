// Copyright © Charlie Howard 2026 All rights reserved
#pragma once

// Plays a short two-note chime through the ES8311 codec and speaker. Returns straight away, the sound plays on its own task.
// Call after touchInit(), which brings up the shared I2C bus.
void soundPlayBoot();
