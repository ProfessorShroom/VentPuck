// Copyright © Charlie Howard 2026 All rights reserved
#include "sound.h"
#include "sdlog.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "ESP_I2S.h"
#include "esp_heap_caps.h"
#include "pin_config.h"

static const uint8_t ES8311_ADDR = 0x18;
static const uint32_t RATE = 16000;
static const float VOLUME = 0.45f;
static const int CHIME_MS = 1000;

static I2SClass i2s;
static bool busy = false;
static uint32_t startDelayMs = 0;

static bool reg(uint8_t r, uint8_t v) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(r);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool codecInit() {
  if (!reg(0x00, 0x1F)) return false;  // reset
  delay(5);
  reg(0x00, 0x00);
  reg(0x01, 0x30);
  reg(0x02, 0x00);
  reg(0x03, 0x10);
  reg(0x16, 0x24);
  reg(0x04, 0x10);
  reg(0x05, 0x00);
  reg(0x0B, 0x00);
  reg(0x0C, 0x00);
  reg(0x10, 0x1F);
  reg(0x11, 0x7F);
  reg(0x00, 0x80);
  reg(0x01, 0x3F);
  reg(0x02, 0x00);
  reg(0x05, 0x00);
  reg(0x03, 0x10);
  reg(0x04, 0x10);
  reg(0x07, 0x00);
  reg(0x08, 0xFF);
  reg(0x06, 0x03);
  reg(0x13, 0x10);
  reg(0x1B, 0x0A);
  reg(0x1C, 0x6A);
  reg(0x09, 0x0C);
  reg(0x0A, 0x0C);
  reg(0x0E, 0x02);
  reg(0x12, 0x00);
  reg(0x14, 0x1A);
  reg(0x0D, 0x01);
  reg(0x15, 0x40);
  reg(0x37, 0x08);
  reg(0x45, 0x00);
  reg(0x31, 0x00);
  reg(0x32, 0xBF);
  return true;
}

static float chime(float t) {
  static const float NOTES[5][2] = {
      {0.00f, 493.88f}, {0.05f, 587.33f}, {0.11f, 659.25f}, {0.19f, 739.99f}, {0.30f, 987.77f}};
  const float ATTACK = 0.012f;
  float s = 0;
  for (int k = 0; k < 5; k++) {
    float u = t - NOTES[k][0];
    if (u < 0) continue;
    float a = u < ATTACK ? 0.5f - 0.5f * cosf((float)M_PI * u / ATTACK) : 1.0f;
    if (k == 4) a *= 0.8f;
    s += a * expf(-u * 11.0f) * sinf(2.0f * (float)M_PI * NOTES[k][1] * u);
  }
  float fadeOut = (CHIME_MS / 1000.0f - t) / 0.08f;
  if (fadeOut > 1.0f) fadeOut = 1.0f;
  if (fadeOut < 0.0f) fadeOut = 0.0f;
  return s * 0.41f * fadeOut;
}

static void writeSilence(int ms) {
  int16_t buf[128] = {0};
  int frames = RATE * ms / 1000;
  for (int i = 0; i < frames; i += 64) i2s.write((const uint8_t*)buf, sizeof buf);
}

static void playTask(void*) {
  uint32_t t0 = millis();
  const int total = RATE * CHIME_MS / 1000;
  int16_t* pcm = (int16_t*)heap_caps_malloc(total * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!pcm) pcm = (int16_t*)heap_caps_malloc(total * sizeof(int16_t), MALLOC_CAP_8BIT);
  if (pcm) {
    for (int i = 0; i < total; i++) pcm[i] = (int16_t)(chime((float)i / RATE) * VOLUME * 32767.0f);
  } else {
    Log.println("Sound: out of memory, no boot sound");
  }
  uint32_t renderMs = millis() - t0;
  uint32_t elapsed = millis() - t0;
  if (startDelayMs > elapsed) vTaskDelay(pdMS_TO_TICKS(startDelayMs - elapsed));

  uint32_t playStart = millis();
  if (pcm) {
    writeSilence(30);
    digitalWrite(PA, HIGH);
    writeSilence(20);

    int16_t buf[256];
    for (int i = 0; i < total;) {
      int n = 0;
      for (; n < 128 && i < total; n++, i++) buf[2 * n] = buf[2 * n + 1] = pcm[i];
      i2s.write((const uint8_t*)buf, n * 4);
    }
    heap_caps_free(pcm);

    writeSilence(150);
  }
  Log.printf("Sound: rendered in %u ms, played in %u ms (chime is %d ms)\n", (unsigned)renderMs,
                (unsigned)(millis() - playStart), CHIME_MS);
  digitalWrite(PA, LOW);
  i2s.end();
  reg(0x32, 0x00);
  reg(0x31, 0x60);
  busy = false;
  vTaskDelete(nullptr);
}

void soundPlayBoot(uint32_t delayMs) {
  if (busy) return;
  startDelayMs = delayMs;
  pinMode(PA, OUTPUT);
  digitalWrite(PA, LOW);

  i2s.setPins(BCLKPIN, WSPIN, DIPIN, -1, MCLKPIN);
  if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Log.println("Sound: I2S init failed");
    return;
  }
  if (!codecInit()) {
    Log.println("Sound: ES8311 not found, no boot sound");
    i2s.end();
    return;
  }
  busy = true;
  if (xTaskCreatePinnedToCore(playTask, "bootSound", 4096, nullptr, 5, nullptr, 0) != pdPASS) {
    Log.println("Sound: couldn't start playback task");
    i2s.end();
    busy = false;
  }
}
