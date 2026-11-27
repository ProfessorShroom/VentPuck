// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <stdbool.h>

void rollbackBoot();
void rollbackLoop();
void rollbackArm(const char* tag);
bool rollbackIsBad(const char* tag);
