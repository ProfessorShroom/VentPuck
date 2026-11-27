// Copyright © Charlie Howard 2026 All rights reserved

#include <lvgl.h>
#include <vector>
#include <cstring>
#include "settings.h"
#include "obd.h"
#include "launch.h"
#include "sdlog.h"
#include "boot.h"

static uint32_t g_fakeMs = 0;
extern "C" uint32_t millis(void) { return g_fakeMs; }

Settings g_settings;
bool g_sdOk = false;
uint8_t* g_logoPng = nullptr;
size_t g_logoLen = 0;
LiveData g_live;
WantData g_want;
DtcStatus g_dtc;
SdLog Log;
size_t SdLog::write(uint8_t) { return 1; }
size_t SdLog::write(const uint8_t*, size_t n) { return n; }

void settingsInit() {}
void settingsSave() {}
int wifiCount() { return 0; }
bool vehicleHasBoost() { return g_settings.vehicle != "genericna" && g_settings.vehicle != "bmwe8x"; }
void logInit() {}
void logApply() {}
void displayInit() {}
void displaySetBrightness(int) {}
void displaySetFlipped(bool) {}
void displayLoop() {}
void touchInit() {}
bool touchPressed() { return false; }
bool touchIdle() { return true; }
void powerOff() {}
void obdStop() {}
bool obdStopped() { return true; }
void updaterRequestNow() {}
void updaterBoot() {}
void obdStart() {}
void launchReset(float) {}
void launchSample(float, uint32_t) {}
LaunchState launchState() { return LAUNCH_IDLE; }
float launchSeconds(uint32_t) { return 0.0f; }

#include "../../src/ui.cpp"

static std::vector<lv_color_t> g_fb(SCREEN * SCREEN);

static void flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  for (int y = a->y1; y <= a->y2; y++) {
    for (int x = a->x1; x <= a->x2; x++) g_fb[y * SCREEN + x] = *px++;
  }
  lv_disp_flush_ready(drv);
}

static void run(int ms) {
  for (int t = 0; t < ms; t += 5) {
    g_fakeMs += 5;
    lv_timer_handler();
  }
}

static bool savePpm(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", SCREEN, SCREEN);
  for (const lv_color_t& c : g_fb) {
    uint32_t v = lv_color_to32(c);
    unsigned char rgb[3] = {(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v};
    fwrite(rgb, 1, 3, f);
  }
  return fclose(f) == 0;
}

int main(int argc, char** argv) {
  const char* outDir = argc > 1 ? argv[1] : ".";

  g_settings.startScreen = "home";
  g_settings.darkMode = argc > 2 && strcmp(argv[2], "dark") == 0;
  g_live.state = OBD_LIVE;
  g_live.oilC = 92;
  g_live.coolantC = 88;
  g_live.baroKpa = 101;
  g_live.mapKpa = 186;
  g_live.rpm = 850;
  g_live.iatC = 24;
  g_live.speedKph = 47 * 1.609344f;
  g_live.batteryV = 14.1f;
  g_live.loadPct = 64;
  g_live.throttlePct = 37;
  g_live.ambientC = 12;
  Preferences().putFloat("best060", 5.82f);

  lv_init();
  static lv_disp_draw_buf_t drawBuf;
  static std::vector<lv_color_t> buf(SCREEN * SCREEN);
  lv_disp_draw_buf_init(&drawBuf, buf.data(), nullptr, SCREEN * SCREEN);
  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res = SCREEN;
  drv.ver_res = SCREEN;
  drv.flush_cb = flush;
  drv.draw_buf = &drawBuf;
  drv.full_refresh = 1;
  lv_disp_drv_register(&drv);

  bootShow();
  bootStart();
  run(1000);
  char bootPath[512];
  snprintf(bootPath, sizeof bootPath, "%s/boot.ppm", outDir);
  if (!savePpm(bootPath)) {
    fprintf(stderr, "Couldn't write %s\n", bootPath);
    return 1;
  }
  printf("Saved %s\n", bootPath);
  while (bootRunning()) run(100);
  run(200);

  int saved = 0;
  for (int p = 0; p < PAGE_COUNT; p++) {
    if (!pageEnabled(p)) continue;
    showPage(p, false);
    lv_obj_invalidate(lv_scr_act());
    run(300);
    char path[512];
    snprintf(path, sizeof path, "%s/page%02d.ppm", outDir, p);
    if (!savePpm(path)) {
      fprintf(stderr, "Couldn't write %s\n", path);
      return 1;
    }
    printf("Saved %s\n", path);
    saved++;
  }
  return saved == PAGE_COUNT ? 0 : 1;
}
