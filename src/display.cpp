// Copyright © Charlie Howard 2026 All rights reserved
//
// Display glue for the Waveshare ESP32-S3-Touch-AMOLED-1.75, following Waveshare's own LVGL

#include "display.h"
#include <Arduino.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"

static Arduino_DataBus* bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
static Arduino_CO5300* gfx = new Arduino_CO5300(bus, LCD_RESET, 0 /* rotation */, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

static lv_disp_draw_buf_t drawBuf;
static const uint32_t drawBufPixels = LCD_WIDTH * LCD_HEIGHT / 10;
static lv_color_t buf1[drawBufPixels + 2048];

static void flushCb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)&color_p->full, w, h);
  lv_disp_flush_ready(disp);
}

static void rounderCb(lv_disp_drv_t*, lv_area_t* area) {
  if (area->x1 % 2 != 0) area->x1--;
  if (area->y1 % 2 != 0) area->y1--;
  if (area->x2 % 2 == 0) area->x2++;
  if (area->y2 % 2 == 0) area->y2++;
}

void displayInit() {
  gfx->begin();
  gfx->setBrightness(0);  // stay dark until the first frame is ready
  gfx->fillScreen(0x0000);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf1, nullptr, drawBufPixels);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = LCD_WIDTH;
  dispDrv.ver_res = LCD_HEIGHT;
  dispDrv.flush_cb = flushCb;
  dispDrv.rounder_cb = rounderCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  // LVGL reads millis() directly (LV_TICK_CUSTOM in lv_conf.h), so its clock can't be starved by the
  // Bluetooth stack starting up on core 0.
}

void displaySetBrightness(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  gfx->setBrightness((uint8_t)(percent * 255 / 100));
}

// Logs any frame that takes longer than 40 ms during the first 8 s, to see how the boot animation performs.
// Set to 1 to enable.
#define FRAME_DEBUG 0

void displayLoop() {
#if FRAME_DEBUG
  uint32_t t0 = millis();
  lv_timer_handler();
  uint32_t dt = millis() - t0;
  if (dt > 40 && t0 < 8000) Serial.printf("slow frame: %u ms at %u ms\n", (unsigned)dt, (unsigned)t0);
#else
  lv_timer_handler();
#endif
}
