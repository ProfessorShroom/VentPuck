// Copyright © Charlie Howard 2026 All rights reserved
#include "power.h"
#include <Arduino.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "display.h"
#include "pin_config.h"

static const gpio_num_t HELD_PINS[] = {(gpio_num_t)TP_RESET, (gpio_num_t)LCD_RESET, (gpio_num_t)LCD_CS, (gpio_num_t)PA};

void powerInit() {
  gpio_deep_sleep_hold_dis();
  for (gpio_num_t pin : HELD_PINS) gpio_hold_dis(pin);
  rtc_gpio_deinit((gpio_num_t)TP_INT);
}

void powerOff() {
  displayOff();
  pinMode(PA, OUTPUT);
  digitalWrite(PA, LOW);
  pinMode(TP_RESET, OUTPUT);
  digitalWrite(TP_RESET, HIGH);
  for (gpio_num_t pin : HELD_PINS) gpio_hold_en(pin);
  gpio_deep_sleep_hold_en();

  gpio_num_t wake = (gpio_num_t)TP_INT;
  detachInterrupt(digitalPinToInterrupt(TP_INT));
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext0_wakeup(wake, 0);
  rtc_gpio_pulldown_dis(wake);
  rtc_gpio_pullup_en(wake);
  esp_deep_sleep_start();
}
