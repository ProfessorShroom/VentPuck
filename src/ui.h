// Copyright © Charlie Howard 2026 All rights reserved
#pragma once
#include <lvgl.h>

void uiBuild(lv_obj_t* scr);

struct ThemeColours {
  uint32_t bgOuter, bgCentre, menuBg, text, icon, btnMain, btnDark, btnPressed;
};
ThemeColours uiTheme();
