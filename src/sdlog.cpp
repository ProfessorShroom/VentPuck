// Copyright © Charlie Howard 2026 All rights reserved
#include "sdlog.h"
#include "FS.h"
#include "SD_MMC.h"
#include "esp_heap_caps.h"
#include "settings.h"
#include "version.h"

#define LOG_PATH "/ventpuck.log"
#define LOG_OLD_PATH "/ventpuck.old.log"

static const size_t BUF_SIZE = 32 * 1024;
static const size_t MAX_FILE = 4 * 1024 * 1024;

SdLog Log;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static char* bufA = nullptr;
static char* bufB = nullptr;
static size_t len = 0;
static size_t dropped = 0;
static volatile bool capture = false;
static TaskHandle_t task = nullptr;
static File file;

size_t SdLog::write(uint8_t c) { return write(&c, 1); }

size_t SdLog::write(const uint8_t* data, size_t n) {
  Serial.write(data, n);
  if (!capture) return n;
  portENTER_CRITICAL(&mux);
  size_t room = BUF_SIZE - len;
  size_t take = n < room ? n : room;
  memcpy(bufA + len, data, take);
  len += take;
  dropped += n - take;
  portEXIT_CRITICAL(&mux);
  return n;
}

static void openFile() {
  if (SD_MMC.exists(LOG_PATH)) {
    File f = SD_MMC.open(LOG_PATH, FILE_READ);
    size_t size = f ? f.size() : 0;
    if (f) f.close();
    if (size > MAX_FILE) {
      SD_MMC.remove(LOG_OLD_PATH);
      SD_MMC.rename(LOG_PATH, LOG_OLD_PATH);
    }
  }
  file = SD_MMC.open(LOG_PATH, FILE_APPEND);
}

static void logTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    portENTER_CRITICAL(&mux);
    char* out = bufA;
    size_t n = len;
    size_t lost = dropped;
    bufA = bufB;
    bufB = out;
    len = 0;
    dropped = 0;
    portEXIT_CRITICAL(&mux);

    if (n > 0 || lost > 0) {
      if (!file) openFile();
      if (file) {
        if (n) file.write((const uint8_t*)out, n);
        if (lost) file.printf("\n[log: %u bytes dropped]\n", (unsigned)lost);
        file.flush();
        if (file.size() > MAX_FILE) file.close();
      }
    } else if (!capture && file) {
      file.close();
    }
  }
}

void logInit() {
  bufA = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bufB = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!bufA || !bufB) {
    heap_caps_free(bufA);
    heap_caps_free(bufB);
    bufA = bufB = nullptr;
    return;
  }
  capture = true;
}

void logApply() {
  if (!bufA) return;
  bool want = g_settings.sdLog && g_sdOk;
  if (!want) {
    portENTER_CRITICAL(&mux);
    capture = false;
    len = 0;
    dropped = 0;
    portEXIT_CRITICAL(&mux);
    return;
  }
  if (!capture) {
    capture = true;
    Log.printf("\n=== VentPuck %s logging on ===\n", FW_VERSION);
  }
  if (!task) xTaskCreatePinnedToCore(logTask, "sdlog", 6144, nullptr, 1, &task, 0);
}
