// Copyright © Charlie Howard 2026 All rights reserved
#pragma once

// Builds the home screen and, if enabled in settings, prepares the boot animation over it.
void bootShow();
// Starts the boot animation (if enabled). Call it right after the screen comes on, so the whole
void bootStart();
// True while the boot animation is playing. Lets setup() wait for it to finish before starting the Bluetooth connection, so the two don't compete for the CPU.
bool bootRunning();
