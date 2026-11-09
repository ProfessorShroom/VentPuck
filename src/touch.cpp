// Copyright © Charlie Howard 2026 All rights reserved
#include "touch.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include "TouchDrvCSTXXX.hpp"
#include "pin_config.h"

#define TOUCH_SWAP_XY 0
#define TOUCH_FLIP_X 1
#define TOUCH_FLIP_Y 1

static TouchDrvCST92xx touch;
static volatile bool touchPending = false;
static bool touchReady = false;
static bool pressed = false;
static int lastX = 0;
static int lastY = 0;

static void IRAM_ATTR onTouchInterrupt() { touchPending = true; }

static void readCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  if (touchReady && (touchPending || pressed)) {
    touchPending = false;
    int16_t xs[1] = {0};
    int16_t ys[1] = {0};
    uint8_t n = touch.getPoint(xs, ys, 1);
    if (n > 0) {
      int x = xs[0];
      int y = ys[0];
      if (TOUCH_SWAP_XY) {
        int t = x;
        x = y;
        y = t;
      }
      if (TOUCH_FLIP_X) x = LCD_WIDTH - 1 - x;
      if (TOUCH_FLIP_Y) y = LCD_HEIGHT - 1 - y;
      if (x < 0) x = 0;
      if (y < 0) y = 0;
      if (x > LCD_WIDTH - 1) x = LCD_WIDTH - 1;
      if (y > LCD_HEIGHT - 1) y = LCD_HEIGHT - 1;
      lastX = x;
      lastY = y;
      pressed = true;
    } else {
      pressed = false;
    }
  }
  data->point.x = lastX;
  data->point.y = lastY;
  data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

void touchInit() {
  touch.setPins(TP_RESET, TP_INT);
  if (!touch.begin(Wire, CST92XX_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    Serial.println("Touch: CST9217 init failed, continuing without touch");
    return;
  }
  Serial.printf("Touch: %s ready\n", touch.getModelName());
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), onTouchInterrupt, FALLING);
  touchReady = true;

  static lv_indev_drv_t drv;
  lv_indev_drv_init(&drv);
  drv.type = LV_INDEV_TYPE_POINTER;
  drv.read_cb = readCb;
  lv_indev_drv_register(&drv);
}

bool touchPressed() { return pressed; }
