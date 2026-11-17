// Copyright © Charlie Howard 2026 All rights reserved
#include "sound.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "ESP_I2S.h"
#include "pin_config.h"

static const uint8_t ES8311_ADDR = 0x18;
static const uint32_t RATE = 16000;  // MCLK = 256 * 16 kHz = 4.096 MHz
static const float VOLUME = 0.35f;  // keeps the small speaker clean
static const int CHIME_MS = 900;

static I2SClass i2s;
static bool busy = false;

static bool reg(uint8_t r, uint8_t v) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(r);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

// ES8311 as I2S slave, DAC only, 16-bit, MCLK from the ESP32 at 4.096 MHz for 16 kHz.
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
  reg(0x00, 0x80);  // power on, slave mode
  reg(0x01, 0x3F);  // all clocks on, MCLK from pin
  // clock dividers for 4.096 MHz / 16 kHz
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
  // serial ports: I2S, 16-bit
  reg(0x09, 0x0C);
  reg(0x0A, 0x0C);
  // power up the DAC path
  reg(0x0E, 0x02);
  reg(0x12, 0x00);
  reg(0x14, 0x1A);
  reg(0x0D, 0x01);
  reg(0x15, 0x40);
  reg(0x37, 0x08);
  reg(0x45, 0x00);
  reg(0x31, 0x00);  // unmute
  reg(0x32, 0xBF);  // DAC volume 0 dB
  return true;
}

// Two bell-like notes, E5 then B5, each with a quick attack and an exponential decay.
static float chime(float t) {
  static const float NOTES[2][2] = {{0.0f, 659.25f}, {0.13f, 987.77f}};
  float s = 0;
  for (auto& n : NOTES) {
    float u = t - n[0];
    if (u < 0) continue;
    float env = (u < 0.005f ? u / 0.005f : 1.0f) * expf(-u * 5.0f);
    float ph = 2.0f * (float)M_PI * n[1] * u;
    s += env * (sinf(ph) + 0.25f * sinf(2.0f * ph));
  }
  float fadeOut = (CHIME_MS / 1000.0f - t) / 0.05f;  // no click at the end
  if (fadeOut > 1.0f) fadeOut = 1.0f;
  if (fadeOut < 0.0f) fadeOut = 0.0f;
  return s * 0.5f * fadeOut;
}

static void writeSilence(int ms) {
  int16_t buf[128] = {0};
  int frames = RATE * ms / 1000;
  for (int i = 0; i < frames; i += 64) i2s.write((const uint8_t*)buf, sizeof buf);
}

static void playTask(void*) {
  writeSilence(30);  // let the DAC settle before the amplifier comes on
  digitalWrite(PA, HIGH);
  writeSilence(20);

  int16_t buf[256];  // 128 stereo frames
  int total = RATE * CHIME_MS / 1000;
  for (int i = 0; i < total;) {
    int n = 0;
    for (; n < 128 && i < total; n++, i++) {
      int16_t v = (int16_t)(chime((float)i / RATE) * VOLUME * 32767.0f);
      buf[2 * n] = v;
      buf[2 * n + 1] = v;
    }
    i2s.write((const uint8_t*)buf, n * 4);
  }

  writeSilence(40);
  digitalWrite(PA, LOW);
  i2s.end();
  reg(0x32, 0x00);  // DAC volume to minimum
  reg(0x31, 0x60);  // mute
  busy = false;
  vTaskDelete(nullptr);
}

void soundPlayBoot() {
  if (busy) return;
  pinMode(PA, OUTPUT);
  digitalWrite(PA, LOW);

  // pin_config names the data pins from the codec's side: DIPIN (GPIO 8) is the codec's input, so it's the ESP32's output.
  i2s.setPins(BCLKPIN, WSPIN, DIPIN, -1, MCLKPIN);
  if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Sound: I2S init failed");
    return;
  }
  if (!codecInit()) {  // needs MCLK running, so after I2S starts
    Serial.println("Sound: ES8311 not found, no boot sound");
    i2s.end();
    return;
  }
  busy = true;
  if (xTaskCreatePinnedToCore(playTask, "bootSound", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
    Serial.println("Sound: couldn't start playback task");
    i2s.end();
    busy = false;
  }
}
