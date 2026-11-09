// Copyright © Charlie Howard 2026 All rights reserved
#include "ui.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "settings.h"
#include "obd.h"
#include "boot.h"
#include "display.h"
#include "touch.h"
#include "icons.h"
#include "launch.h"
#include "updater.h"
#include "version.h"

LV_FONT_DECLARE(font_digits);

static const int SCREEN = 466;
static const int METRIC_COUNT = 6;
static const int PAGE_COUNT = METRIC_COUNT + 3;
static const int RUN_PAGE = METRIC_COUNT + 1;
static const int FAULT_PAGE = METRIC_COUNT + 2;
static const int HOME_SLOTS = 4;
static const int MENU_ROWS = 16;

struct MetricDef {
  const char* key;
  const char* label;
  const lv_img_dsc_t* icon;
};

static const MetricDef METRICS[METRIC_COUNT] = {
    {"boost", "Boost", &iconBoost},   {"oil", "Oil", &iconOil},       {"coolant", "Coolant", &iconCoolant},
    {"rpm", "RPM", &iconRpm},         {"speed", "Speed", &iconSpeed}, {"voltage", "Voltage", &iconVoltage}};

static const char* START_OPTIONS[] = {"last", "home", "boost", "oil", "coolant", "rpm", "speed", "voltage", "zerosixty", "faults"};
static const int START_OPTION_COUNT = 10;
static const int BRIGHTNESS_STEPS[] = {20, 40, 60, 80, 100};

static lv_color_t* gradBuf = nullptr;
static lv_obj_t* gradCanvas = nullptr;
static lv_obj_t* statusLbl;
static lv_obj_t* pageObj[PAGE_COUNT];
static lv_obj_t* dotObj[PAGE_COUNT];
static lv_obj_t* homeIcon[HOME_SLOTS];
static lv_obj_t* homeLabel[HOME_SLOTS];
static lv_obj_t* homeValue[HOME_SLOTS];
static lv_obj_t* pageTitle[METRIC_COUNT];
static lv_obj_t* pageValue[METRIC_COUNT];
static lv_obj_t* stripOilIcon;
static lv_obj_t* stripOilVal;
static lv_obj_t* stripCoolIcon;
static lv_obj_t* stripCoolVal;
static lv_obj_t* menuObj;
static lv_obj_t* runTitle;
static lv_obj_t* runValue;
static lv_obj_t* runStatus;
static lv_obj_t* runBest;
static lv_obj_t* faultStatus;
static lv_obj_t* faultIcon;
static lv_obj_t* betaModal = nullptr;
static lv_obj_t* animNew = nullptr;
static lv_obj_t* animOld = nullptr;
static int animDir = 0;
static const int SLIDE_MS = 240;
static lv_obj_t* faultCodes;
static lv_obj_t* clearBar;
static lv_obj_t* clearBtn;
static lv_timer_t* runTimer = nullptr;
static lv_timer_t* faultTimer = nullptr;
static uint32_t lastStamp = 0;
static float bestSeconds = 0.0f;
static bool clearHolding = false;
static uint32_t clearStart = 0;
static lv_obj_t* menuLabel[MENU_ROWS];
static lv_timer_t* pageSaveTimer = nullptr;
static lv_timer_t* settingsSaveTimer = nullptr;
static int curPage = 0;
static bool menuOpen = false;
static bool menuArmed = false;
static lv_timer_t* armTimer = nullptr;
static uint32_t releasedAt = 0;
static String vehicleAtOpen;

static const lv_color_t LAVENDER = LV_COLOR_MAKE(0xD9, 0xB8, 0xFF);

static int metricIndex(const String& key) {
  for (int i = 0; i < METRIC_COUNT; i++) {
    if (key == METRICS[i].key) return i;
  }
  return 0;
}

static void setText(lv_obj_t* l, const char* t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

static void upperCopy(char* dst, size_t n, const char* src) {
  size_t i = 0;
  for (; src[i] && i + 1 < n; i++) dst[i] = toupper((unsigned char)src[i]);
  dst[i] = 0;
}

static void tempText(float c, char* out, size_t n) {
  if (isnan(c)) {
    snprintf(out, n, "--");
    return;
  }
  bool f = g_settings.tempUnit == "F";
  snprintf(out, n, "%.0f", f ? c * 9.0f / 5.0f + 32.0f : c);
}

static void metricText(int m, char* val, size_t vn, char* unit, size_t un) {
  snprintf(val, vn, "--");
  unit[0] = 0;
  bool f = g_settings.tempUnit == "F";
  switch (m) {
    case 0: {
      float map = g_live.mapKpa;
      float baro = g_live.baroKpa;
      if (isnan(baro)) baro = 101.3f;
      float kpa = map - baro;
      if (fabsf(kpa) < 0.5f) kpa = 0.0f;
      if (g_settings.boostUnit == "psi") {
        snprintf(unit, un, "psi");
        if (!isnan(map)) snprintf(val, vn, "%.1f", kpa * 0.145038f);
      } else if (g_settings.boostUnit == "kpa") {
        snprintf(unit, un, "kPa");
        if (!isnan(map)) snprintf(val, vn, "%.0f", kpa);
      } else {
        snprintf(unit, un, "bar");
        if (!isnan(map)) snprintf(val, vn, "%.2f", kpa / 100.0f);
      }
      break;
    }
    case 1:
    case 2: {
      float t = m == 1 ? g_live.oilC : g_live.coolantC;
      tempText(t, val, vn);
      snprintf(unit, un, f ? "°F" : "°C");
      break;
    }
    case 3: {
      float r = g_live.rpm;
      if (!isnan(r)) snprintf(val, vn, "%.0f", r);
      snprintf(unit, un, "rpm");
      break;
    }
    case 4: {
      float kph = g_live.speedKph;
      bool mph = g_settings.speedUnit == "mph";
      if (!isnan(kph)) snprintf(val, vn, "%.0f", mph ? kph * 0.621371f : kph);
      snprintf(unit, un, mph ? "mph" : "km/h");
      break;
    }
    default: {
      float v = g_live.batteryV;
      if (!isnan(v)) snprintf(val, vn, "%.1f", v);
      snprintf(unit, un, "V");
      break;
    }
  }
}

static float runTargetKph() { return g_settings.speedUnit == "mph" ? 96.5606f : 100.0f; }

static const char* bestKey() { return g_settings.speedUnit == "mph" ? "best060" : "best0100"; }

static void loadBest() {
  Preferences p;
  bestSeconds = 0.0f;
  if (p.begin("ventpuck", true)) {
    bestSeconds = p.getFloat(bestKey(), 0.0f);
    p.end();
  }
}

static void updateRunLabels() {
  setText(runTitle, g_settings.speedUnit == "mph" ? "0-60  MPH" : "0-100  KM/H");
  char buf[32];
  snprintf(buf, sizeof buf, "%.2f", launchSeconds(millis()));
  setText(runValue, buf);
  const char* status = "Waiting for speed...";
  switch (launchState()) {
    case LAUNCH_IDLE: break;
    case LAUNCH_ARMED: status = "Ready - go!"; break;
    case LAUNCH_RUNNING: status = "Go!"; break;
    case LAUNCH_DONE: status = "Tap to reset"; break;
  }
  setText(runStatus, status);
  if (bestSeconds > 0.0f) snprintf(buf, sizeof buf, "BEST  %.2f s", bestSeconds);
  else snprintf(buf, sizeof buf, "BEST  --");
  setText(runBest, buf);
}

static void runCb(lv_timer_t*) {
  uint32_t stamp = g_live.speedStamp;
  float kph = g_live.speedKph;
  if (stamp != lastStamp && !isnan(kph)) {
    lastStamp = stamp;
    LaunchState before = launchState();
    launchSample(kph, stamp);
    if (before != LAUNCH_DONE && launchState() == LAUNCH_DONE) {
      float t = launchSeconds(0);
      if (bestSeconds <= 0.0f || t < bestSeconds) {
        bestSeconds = t;
        Preferences p;
        if (p.begin("ventpuck", false)) {
          p.putFloat(bestKey(), t);
          p.end();
        }
      }
    }
  }
  updateRunLabels();
}

static void updateFaultLabels() {
  const char* status = "Press Scan";
  switch (g_dtc.state) {
    case DTCS_IDLE: break;
    case DTCS_SCANNING: status = "Working..."; break;
    case DTCS_DONE: status = g_dtc.count == 0 ? "No fault codes" : "Fault codes found"; break;
    case DTCS_ERROR: status = "No reply from the car"; break;
    case DTCS_CLEARED: status = "Cleared. Scan to check"; break;
  }
  setText(faultStatus, status);
  bool bad = g_dtc.state == DTCS_DONE && g_dtc.count > 0;
  lv_obj_set_style_img_recolor(faultIcon, bad ? lv_color_hex(0xFF5A5A) : lv_color_hex(0xEAD9FF), 0);
  char buf[96] = "";
  if (g_dtc.state == DTCS_DONE) {
    int shown = g_dtc.count < 9 ? g_dtc.count : 9;
    for (int i = 0; i < shown; i++) {
      strcat(buf, g_dtc.codes[i]);
      strcat(buf, i % 3 == 2 ? "\n" : "  ");
    }
  }
  setText(faultCodes, buf);
  if (buf[0]) lv_obj_add_flag(faultIcon, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(faultIcon, LV_OBJ_FLAG_HIDDEN);
}

static void faultCb(lv_timer_t*) {
  if (!clearHolding) return;
  uint32_t held = lv_tick_elaps(clearStart);
  int pct = held >= 1500 ? 100 : (int)(held * 100 / 1500);
  lv_bar_set_value(clearBar, pct, LV_ANIM_OFF);
  if (held < 1500) return;
  clearHolding = false;
  lv_bar_set_value(clearBar, 0, LV_ANIM_OFF);
  float kph = g_live.speedKph;
  if (!isnan(kph) && kph > 3.0f) {
    setText(faultStatus, "Stop the car first");
    return;
  }
  g_dtc.state = DTCS_SCANNING;
  g_dtc.command = DTC_CLEAR;
}

static void scanEvent(lv_event_t*) {
  if (g_dtc.state == DTCS_SCANNING) return;
  g_dtc.state = DTCS_SCANNING;
  g_dtc.command = DTC_SCAN;
}

static void clearEvent(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    clearHolding = true;
    clearStart = lv_tick_get();
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    clearHolding = false;
    lv_bar_set_value(clearBar, 0, LV_ANIM_OFF);
  }
}

static void updateWant() {
  uint32_t mainMask = 0;
  uint32_t secMask = 0;
  if (curPage == 0) {
    for (int i = 0; i < HOME_SLOTS; i++) mainMask |= 1u << metricIndex(g_settings.homeItems[i]);
  } else if (curPage <= METRIC_COUNT) {
    mainMask = 1u << (curPage - 1);
    secMask = (1u << M_OIL) | (1u << M_COOLANT);
  } else if (curPage == RUN_PAGE) {
    mainMask = 1u << M_SPEED;
  }
  g_want.secondary = secMask;
  g_want.main = mainMask;
}

static void pageEntered(int p) {
  if (p == RUN_PAGE) {
    loadBest();
    launchReset(runTargetKph());
    lastStamp = g_live.speedStamp;
    lv_timer_resume(runTimer);
  } else {
    lv_timer_pause(runTimer);
  }
  if (p == FAULT_PAGE) {
    lv_timer_resume(faultTimer);
  } else {
    lv_timer_pause(faultTimer);
    clearHolding = false;
    lv_bar_set_value(clearBar, 0, LV_ANIM_OFF);
  }
}

static void refreshValues() {
  const char* status = "";
  switch (g_live.state) {
    case OBD_STARTING: status = "Starting..."; break;
    case OBD_SEARCHING: status = "Looking for adapter..."; break;
    case OBD_CONNECTED: status = "Connected"; break;
    case OBD_LIVE: status = ""; break;
  }
  setText(statusLbl, status);
  if (curPage == RUN_PAGE) {
    updateRunLabels();
    return;
  }
  if (curPage == FAULT_PAGE) {
    updateFaultLabels();
    return;
  }

  char val[16];
  char unit[8];
  char buf[40];
  if (curPage == 0) {
    for (int i = 0; i < HOME_SLOTS; i++) {
      int m = metricIndex(g_settings.homeItems[i]);
      metricText(m, val, sizeof val, unit, sizeof unit);
      snprintf(buf, sizeof buf, "%s %s", val, unit);
      setText(homeValue[i], buf);
    }
    return;
  }
  int m = curPage - 1;
  metricText(m, val, sizeof val, unit, sizeof unit);
  char up[16];
  char upUnit[8];
  upperCopy(up, sizeof up, METRICS[m].label);
  upperCopy(upUnit, sizeof upUnit, unit);
  if (m == 3 || m == 5) snprintf(buf, sizeof buf, "%s", up);
  else snprintf(buf, sizeof buf, "%s  %s", up, upUnit);
  setText(pageTitle[m], buf);
  setText(pageValue[m], val);

  char t[12];
  tempText(g_live.oilC, t, sizeof t);
  snprintf(buf, sizeof buf, "%s°", t);
  setText(stripOilVal, buf);
  tempText(g_live.coolantC, t, sizeof t);
  snprintf(buf, sizeof buf, "%s°", t);
  setText(stripCoolVal, buf);
}

static void refreshCb(lv_timer_t*) { refreshValues(); }

static void savePageCb(lv_timer_t*) {
  pageSaveTimer = nullptr;
  Preferences p;
  if (p.begin("ventpuck", false)) {
    p.putInt("lastPage", curPage);
    p.end();
  }
}

static void schedulePageSave() {
  if (pageSaveTimer) {
    lv_timer_reset(pageSaveTimer);
    return;
  }
  pageSaveTimer = lv_timer_create(savePageCb, 3000, nullptr);
  lv_timer_set_repeat_count(pageSaveTimer, 1);
}

static void saveSettingsCb(lv_timer_t*) {
  settingsSaveTimer = nullptr;
  settingsSave();
}

static void settingsSaveSoon() {
  if (settingsSaveTimer) {
    lv_timer_reset(settingsSaveTimer);
    return;
  }
  settingsSaveTimer = lv_timer_create(saveSettingsCb, 1500, nullptr);
  lv_timer_set_repeat_count(settingsSaveTimer, 1);
}

static void slideExec(void*, int32_t v) {
  if (animNew) lv_obj_set_x(animNew, animDir * (SCREEN - v));
  if (animOld) lv_obj_set_x(animOld, -animDir * v);
}

static void slideSettle() {
  if (animOld) {
    lv_obj_add_flag(animOld, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(animOld, 0);
  }
  if (animNew) lv_obj_set_x(animNew, 0);
  animOld = nullptr;
  animNew = nullptr;
}

static void slideFinish() {
  lv_anim_del(&animDir, slideExec);
  slideSettle();
}

static void slideDone(lv_anim_t*) { slideSettle(); }

static void showPage(int p, bool save, int dir = 0) {
  if (p < 0) p = PAGE_COUNT - 1;
  if (p >= PAGE_COUNT) p = 0;
  if (animNew) slideFinish();
  int prev = curPage;
  curPage = p;
  bool slide = dir != 0 && prev != p && prev >= 0 && prev < PAGE_COUNT;
  for (int i = 0; i < PAGE_COUNT; i++) {
    bool keep = i == p || (slide && i == prev);
    if (keep) lv_obj_clear_flag(pageObj[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(pageObj[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(dotObj[i], i == p ? LV_OPA_COVER : LV_OPA_30, 0);
  }
  lv_obj_t* strip[] = {stripOilIcon, stripOilVal, stripCoolIcon, stripCoolVal};
  for (lv_obj_t* o : strip) {
    if (p == 0 || p > METRIC_COUNT) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  updateWant();
  pageEntered(p);
  refreshValues();
  if (slide) {
    animNew = pageObj[p];
    animOld = pageObj[prev];
    animDir = dir;
    slideExec(nullptr, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &animDir);
    lv_anim_set_exec_cb(&a, slideExec);
    lv_anim_set_values(&a, 0, SCREEN);
    lv_anim_set_time(&a, SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, slideDone);
    lv_anim_start(&a);
  }
  if (save) schedulePageSave();
}

static void applyHomeSlots() {
  for (int i = 0; i < HOME_SLOTS; i++) {
    int m = metricIndex(g_settings.homeItems[i]);
    lv_img_set_src(homeIcon[i], METRICS[m].icon);
    lv_label_set_text(homeLabel[i], METRICS[m].label);
  }
}

static void capitalise(char* s) {
  if (s[0]) s[0] = toupper((unsigned char)s[0]);
}

static const char* vehicleLabel() {
  if (g_settings.vehicle == "meganers3") return "Megane 3 RS";
  if (g_settings.vehicle == "bmwe8x") return "BMW E8x";
  return "Generic";
}

static void menuText(int id, char* out, size_t n) {
  char tmp[24];
  switch (id) {
    case 0:
      snprintf(out, n, "Close");
      break;
    case 1:
      snprintf(tmp, sizeof tmp, "%s", vehicleLabel());
      snprintf(out, n, "Car: %s", tmp);
      break;
    case 2:
      snprintf(out, n, "Speed: %s", g_settings.speedUnit == "mph" ? "mph" : "km/h");
      break;
    case 3:
      snprintf(out, n, "Boost: %s", g_settings.boostUnit == "kpa" ? "kPa" : g_settings.boostUnit.c_str());
      break;
    case 4:
      snprintf(out, n, "Temperature: °%s", g_settings.tempUnit.c_str());
      break;
    case 5:
      snprintf(out, n, "Brightness: %d%%", g_settings.brightness);
      break;
    case 6:
      if (g_settings.startScreen == "last") snprintf(tmp, sizeof tmp, "Last used");
      else if (g_settings.startScreen == "zerosixty") snprintf(tmp, sizeof tmp, "0-60");
      else if (g_settings.startScreen == "faults") snprintf(tmp, sizeof tmp, "Fault codes");
      else snprintf(tmp, sizeof tmp, "%s", g_settings.startScreen.c_str());
      capitalise(tmp);
      snprintf(out, n, "Start: %s", tmp);
      break;
    case 11:
      snprintf(out, n, "Boot animation: %s", g_settings.bootAnimation ? "On" : "Off");
      break;
    case 12:
      snprintf(out, n, "Dark mode: %s", g_settings.darkMode ? "On" : "Off");
      break;
    case 13:
      snprintf(out, n, "Updates: %s", g_settings.updateChannel == "beta" ? "Beta" : "Release");
      break;
    case 14:
      snprintf(out, n, "Check for updates now");
      break;
    case 15:
      snprintf(out, n, "Version %s", FW_VERSION);
      break;
    default: {
      int slot = id - 7;
      snprintf(out, n, "Home slot %d: %s", slot + 1, METRICS[metricIndex(g_settings.homeItems[slot])].label);
      break;
    }
  }
}

static void refreshMenu() {
  char buf[48];
  for (int i = 0; i < MENU_ROWS; i++) {
    menuText(i, buf, sizeof buf);
    lv_label_set_text(menuLabel[i], buf);
  }
}

static void armCb(lv_timer_t* t) {
  if (touchPressed()) {
    releasedAt = 0;
    return;
  }
  if (releasedAt == 0) {
    releasedAt = lv_tick_get();
    return;
  }
  if (lv_tick_elaps(releasedAt) > 250) {
    menuArmed = true;
    armTimer = nullptr;
    lv_timer_del(t);
  }
}

static void openMenu() {
  menuArmed = false;
  releasedAt = 0;
  armTimer = lv_timer_create(armCb, 30, nullptr);
  vehicleAtOpen = g_settings.vehicle;
  refreshMenu();
  lv_obj_scroll_to_y(menuObj, 0, LV_ANIM_OFF);
  lv_obj_clear_flag(menuObj, LV_OBJ_FLAG_HIDDEN);
  menuOpen = true;
}

static void closeMenu() {
  if (armTimer) {
    lv_timer_del(armTimer);
    armTimer = nullptr;
  }
  lv_obj_add_flag(menuObj, LV_OBJ_FLAG_HIDDEN);
  menuOpen = false;
  if (settingsSaveTimer) {
    lv_timer_del(settingsSaveTimer);
    settingsSaveTimer = nullptr;
  }
  settingsSave();
  if (curPage == RUN_PAGE) {
    loadBest();
    launchReset(runTargetKph());
  }
  updateWant();
  if (g_settings.vehicle != vehicleAtOpen) {
    Serial.println("Vehicle changed, restarting");
    delay(300);
    ESP.restart();
  }
}

static void applyTheme() {
  bool dark = g_settings.darkMode;
  lv_obj_set_style_bg_color(lv_scr_act(), dark ? lv_color_hex(0x000000) : lv_color_hex(g_settings.colour1), 0);
  if (gradCanvas) {
    if (dark) lv_obj_add_flag(gradCanvas, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(gradCanvas, LV_OBJ_FLAG_HIDDEN);
  }
  if (menuObj) lv_obj_set_style_bg_color(menuObj, dark ? lv_color_hex(0x000000) : lv_color_hex(0x0D0620), 0);
}

static void setChannel(const char* channel) {
  g_settings.updateChannel = channel;
  char buf[48];
  menuText(13, buf, sizeof buf);
  lv_label_set_text(menuLabel[13], buf);
  settingsSaveSoon();
}

static void betaCancel(lv_event_t*) { lv_obj_add_flag(betaModal, LV_OBJ_FLAG_HIDDEN); }

static void betaAccept(lv_event_t*) {
  lv_obj_add_flag(betaModal, LV_OBJ_FLAG_HIDDEN);
  setChannel("beta");
}

static lv_obj_t* modalButton(lv_obj_t* parent, const char* text, int y, uint32_t colour, lv_event_cb_t cb) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, 260, 52);
  lv_obj_set_pos(b, 103, y);
  lv_obj_set_style_radius(b, 26, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(colour), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x8A5CFF), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

static void buildBetaModal(lv_obj_t* scr) {
  betaModal = lv_obj_create(scr);
  lv_obj_remove_style_all(betaModal);
  lv_obj_set_size(betaModal, SCREEN, SCREEN);
  lv_obj_set_pos(betaModal, 0, 0);
  lv_obj_set_style_bg_color(betaModal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(betaModal, LV_OPA_COVER, 0);
  lv_obj_clear_flag(betaModal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* title = lv_label_create(betaModal);
  lv_obj_set_width(title, 340);
  lv_obj_set_pos(title, 63, 62);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFF8A5A), 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(title, "BETA");
  lv_obj_t* body = lv_label_create(betaModal);
  lv_obj_set_width(body, 320);
  lv_obj_set_pos(body, 73, 110);
  lv_obj_set_style_text_font(body, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(body, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_label_set_text(body, "Beta versions are test builds. They may be unstable, have bugs, or stop working. Use at your own risk.");
  modalButton(betaModal, "Cancel", 290, 0x5A1FE0, betaCancel);
  modalButton(betaModal, "Use Beta", 352, 0x2A0A78, betaAccept);
  lv_obj_add_flag(betaModal, LV_OBJ_FLAG_HIDDEN);
}

static void menuClick(lv_event_t* e) {
  if (!menuArmed) return;
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  if (id == 0) {
    closeMenu();
    return;
  }
  if (id == 15) return;
  if (id == 13) {
    if (g_settings.updateChannel == "beta") setChannel("release");
    else lv_obj_clear_flag(betaModal, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (id == 14) {
    settingsSave();
    updaterRequestNow();
    Serial.println("Update check requested, restarting");
    delay(300);
    ESP.restart();
    return;
  }
  if (id == 1) {
    if (g_settings.vehicle == "generic") g_settings.vehicle = "meganers3";
    else if (g_settings.vehicle == "meganers3") g_settings.vehicle = "bmwe8x";
    else g_settings.vehicle = "generic";
  } else if (id == 2) {
    g_settings.speedUnit = g_settings.speedUnit == "mph" ? "kph" : "mph";
  } else if (id == 3) {
    if (g_settings.boostUnit == "bar") g_settings.boostUnit = "psi";
    else if (g_settings.boostUnit == "psi") g_settings.boostUnit = "kpa";
    else g_settings.boostUnit = "bar";
  } else if (id == 4) {
    g_settings.tempUnit = g_settings.tempUnit == "C" ? "F" : "C";
  } else if (id == 5) {
    int next = BRIGHTNESS_STEPS[0];
    for (int b : BRIGHTNESS_STEPS) {
      if (b > g_settings.brightness) {
        next = b;
        break;
      }
    }
    g_settings.brightness = next;
    displaySetBrightness(next);
  } else if (id == 6) {
    int cur = 0;
    for (int i = 0; i < START_OPTION_COUNT; i++) {
      if (g_settings.startScreen == START_OPTIONS[i]) cur = i;
    }
    g_settings.startScreen = START_OPTIONS[(cur + 1) % START_OPTION_COUNT];
  } else if (id == 11) {
    g_settings.bootAnimation = !g_settings.bootAnimation;
  } else if (id == 12) {
    g_settings.darkMode = !g_settings.darkMode;
    applyTheme();
  } else {
    int slot = id - 7;
    int next = (metricIndex(g_settings.homeItems[slot]) + 1) % METRIC_COUNT;
    g_settings.homeItems[slot] = METRICS[next].key;
    applyHomeSlots();
    updateWant();
  }
  char buf[48];
  menuText(id, buf, sizeof buf);
  lv_label_set_text(menuLabel[id], buf);
  refreshValues();
  settingsSaveSoon();
}

static void rootEvent(lv_event_t* e) {
  if (bootRunning() || menuOpen) return;
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_GESTURE) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT) showPage(curPage + 1, true, 1);
    else if (dir == LV_DIR_RIGHT) showPage(curPage - 1, true, -1);
  } else if (code == LV_EVENT_LONG_PRESSED) {
    openMenu();
  } else if (code == LV_EVENT_CLICKED && curPage == RUN_PAGE) {
    launchReset(runTargetKph());
    lastStamp = g_live.speedStamp;
  }
}

static void buildBackground(lv_obj_t* scr) {
  lv_color_t outer = lv_color_hex(g_settings.colour1);
  lv_color_t centre = lv_color_hex(g_settings.colour2);
  lv_obj_set_style_bg_color(scr, outer, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  gradBuf = (lv_color_t*)heap_caps_malloc(SCREEN * SCREEN * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!gradBuf) {
    Serial.println("UI: couldn't allocate the gradient, using a flat colour");
    return;
  }
  const float gx = 233.0f, gy = 210.0f, maxd = 246.0f;
  for (int y = 0; y < SCREEN; y++) {
    for (int x = 0; x < SCREEN; x++) {
      float dx = x - gx, dy = y - gy;
      float t = sqrtf(dx * dx + dy * dy) / maxd;
      if (t > 1.0f) t = 1.0f;
      gradBuf[y * SCREEN + x] = lv_color_mix(outer, centre, (uint8_t)(t * 255.0f));
    }
  }
  lv_obj_t* cv = lv_canvas_create(scr);
  lv_canvas_set_buffer(cv, gradBuf, SCREEN, SCREEN, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(cv, 0, 0);
  gradCanvas = cv;
}

static lv_obj_t* makeContainer(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, SCREEN, SCREEN);
  lv_obj_set_pos(o, 0, 0);
  lv_obj_clear_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

static lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t col, int x, int y, int w,
                           lv_text_align_t align) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_width(l, w);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_set_style_text_align(l, align, 0);
  return l;
}

static lv_obj_t* makeIcon(lv_obj_t* parent, const lv_img_dsc_t* src, int x, int y) {
  lv_obj_t* img = lv_img_create(parent);
  lv_img_set_src(img, src);
  lv_obj_set_pos(img, x, y);
  lv_obj_set_style_img_recolor(img, lv_color_hex(0xEAD9FF), 0);
  lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
  return img;
}

static void buildHomePage(lv_obj_t* scr) {
  pageObj[0] = makeContainer(scr);
  for (int i = 0; i < HOME_SLOTS; i++) {
    int top = 96 + i * 76;
    int m = metricIndex(g_settings.homeItems[i]);
    homeIcon[i] = makeIcon(pageObj[0], METRICS[m].icon, 76, top + 14);
    homeLabel[i] = makeLabel(pageObj[0], &lv_font_montserrat_28, LAVENDER, 140, top + 20, 150, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(homeLabel[i], METRICS[m].label);
    homeValue[i] = makeLabel(pageObj[0], &lv_font_montserrat_28, lv_color_hex(0xFFFFFF), 176, top + 20, 220, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(homeValue[i], "--");
    if (i < HOME_SLOTS - 1) {
      lv_obj_t* line = lv_obj_create(pageObj[0]);
      lv_obj_remove_style_all(line);
      lv_obj_set_size(line, 340, 2);
      lv_obj_set_pos(line, 63, top + 74);
      lv_obj_set_style_bg_color(line, LAVENDER, 0);
      lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
    }
  }
}

static void buildMetricPages(lv_obj_t* scr) {
  for (int m = 0; m < METRIC_COUNT; m++) {
    pageObj[m + 1] = makeContainer(scr);
    pageTitle[m] = makeLabel(pageObj[m + 1], &lv_font_montserrat_28, LAVENDER, 0, 114, SCREEN, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(pageTitle[m], 5, 0);
    lv_label_set_text(pageTitle[m], METRICS[m].label);
    pageValue[m] = makeLabel(pageObj[m + 1], &font_digits, lv_color_hex(0xFFFFFF), 0, 148, SCREEN, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(pageValue[m], "--");
  }
  stripOilIcon = makeIcon(scr, &iconOil, 96, 343);
  stripOilVal = makeLabel(scr, &lv_font_montserrat_28, lv_color_hex(0xFFFFFF), 150, 349, 80, LV_TEXT_ALIGN_LEFT);
  stripCoolIcon = makeIcon(scr, &iconCoolant, 244, 343);
  stripCoolVal = makeLabel(scr, &lv_font_montserrat_28, lv_color_hex(0xFFFFFF), 298, 349, 80, LV_TEXT_ALIGN_LEFT);
  lv_label_set_text(stripOilVal, "--°");
  lv_label_set_text(stripCoolVal, "--°");
}

static void buildRunPage(lv_obj_t* scr) {
  pageObj[RUN_PAGE] = makeContainer(scr);
  runTitle = makeLabel(pageObj[RUN_PAGE], &lv_font_montserrat_28, LAVENDER, 0, 114, SCREEN, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_style_text_letter_space(runTitle, 5, 0);
  runValue = makeLabel(pageObj[RUN_PAGE], &font_digits, lv_color_hex(0xFFFFFF), 0, 148, SCREEN, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(runValue, "0.00");
  runStatus = makeLabel(pageObj[RUN_PAGE], &lv_font_montserrat_20, lv_color_hex(0xFFFFFF), 0, 300, SCREEN, LV_TEXT_ALIGN_CENTER);
  runBest = makeLabel(pageObj[RUN_PAGE], &lv_font_montserrat_28, LAVENDER, 0, 345, SCREEN, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(runTitle, "0-60  MPH");
  lv_label_set_text(runStatus, "");
  lv_label_set_text(runBest, "");
}

static lv_obj_t* makeButton(lv_obj_t* parent, int x, int y, int w, int h, uint32_t colour) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_radius(b, h / 2, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(colour), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x8A5CFF), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(b, 0, 0);
  return b;
}

static void buildFaultPage(lv_obj_t* scr) {
  pageObj[FAULT_PAGE] = makeContainer(scr);
  faultIcon = makeIcon(pageObj[FAULT_PAGE], &iconEngine, 209, 190);
  lv_obj_t* title = makeLabel(pageObj[FAULT_PAGE], &lv_font_montserrat_28, LAVENDER, 0, 96, SCREEN, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_style_text_letter_space(title, 5, 0);
  lv_label_set_text(title, "FAULT CODES");
  faultStatus = makeLabel(pageObj[FAULT_PAGE], &lv_font_montserrat_20, lv_color_hex(0xFFFFFF), 0, 138, SCREEN, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(faultStatus, "Press Scan");
  faultCodes = makeLabel(pageObj[FAULT_PAGE], &lv_font_montserrat_28, lv_color_hex(0xFFFFFF), 63, 170, 340, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(faultCodes, "");

  lv_obj_t* scanBtn = makeButton(pageObj[FAULT_PAGE], 103, 262, 260, 56, 0x5A1FE0);
  lv_obj_add_event_cb(scanBtn, scanEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* scanLbl = lv_label_create(scanBtn);
  lv_obj_set_style_text_font(scanLbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(scanLbl, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(scanLbl, "Scan");
  lv_obj_center(scanLbl);

  clearBtn = makeButton(pageObj[FAULT_PAGE], 103, 332, 260, 56, 0x2A0A78);
  lv_obj_add_event_cb(clearBtn, clearEvent, LV_EVENT_ALL, nullptr);
  clearBar = lv_bar_create(clearBtn);
  lv_obj_set_size(clearBar, 260, 56);
  lv_obj_center(clearBar);
  lv_bar_set_range(clearBar, 0, 100);
  lv_bar_set_value(clearBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_opa(clearBar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_radius(clearBar, 28, LV_PART_MAIN);
  lv_obj_set_style_radius(clearBar, 28, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(clearBar, lv_color_hex(0xFF5A5A), LV_PART_INDICATOR);
  lv_obj_clear_flag(clearBar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* clearLbl = lv_label_create(clearBtn);
  lv_obj_set_style_text_font(clearLbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(clearLbl, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(clearLbl, "Hold to clear");
  lv_obj_center(clearLbl);
}

static void buildChrome(lv_obj_t* scr) {
  statusLbl = makeLabel(scr, &lv_font_montserrat_20, lv_color_hex(0xFFFFFF), 0, 60, SCREEN, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(statusLbl, "");
  int startX = SCREEN / 2 - (PAGE_COUNT * 10 + (PAGE_COUNT - 1) * 10) / 2;
  for (int i = 0; i < PAGE_COUNT; i++) {
    dotObj[i] = lv_obj_create(scr);
    lv_obj_remove_style_all(dotObj[i]);
    lv_obj_set_size(dotObj[i], 10, 10);
    lv_obj_set_pos(dotObj[i], startX + i * 20, 418);
    lv_obj_set_style_radius(dotObj[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dotObj[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(dotObj[i], LV_OPA_30, 0);
    lv_obj_clear_flag(dotObj[i], LV_OBJ_FLAG_CLICKABLE);
  }
}

static void buildMenu(lv_obj_t* scr) {
  menuObj = lv_obj_create(scr);
  lv_obj_remove_style_all(menuObj);
  lv_obj_set_size(menuObj, SCREEN, SCREEN);
  lv_obj_set_pos(menuObj, 0, 0);
  lv_obj_set_style_bg_color(menuObj, lv_color_hex(0x0D0620), 0);
  lv_obj_set_style_bg_opa(menuObj, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(menuObj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(menuObj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(menuObj, 90, 0);
  lv_obj_set_style_pad_bottom(menuObj, 90, 0);
  lv_obj_set_style_pad_row(menuObj, 12, 0);
  lv_obj_set_scroll_dir(menuObj, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(menuObj, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < MENU_ROWS; i++) {
    lv_obj_t* b = lv_btn_create(menuObj);
    lv_obj_set_size(b, 340, 58);
    lv_obj_set_style_radius(b, 29, 0);
    lv_obj_set_style_bg_color(b, i == 0 ? lv_color_hex(0x5A1FE0) : lv_color_hex(0x2A0A78), 0);
    if (i == 15) {
      lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
      lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_style_bg_color(b, lv_color_hex(0x8A5CFF), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, menuClick, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    menuLabel[i] = lv_label_create(b);
    lv_obj_set_style_text_font(menuLabel[i], &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(menuLabel[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(menuLabel[i]);
  }
  refreshMenu();
  lv_obj_add_flag(menuObj, LV_OBJ_FLAG_HIDDEN);
  buildBetaModal(scr);
}

static int startPage() {
  const String& s = g_settings.startScreen;
  if (s == "home") return 0;
  if (s == "last") {
    int p = 0;
    Preferences pr;
    if (pr.begin("ventpuck", true)) {
      p = pr.getInt("lastPage", 0);
      pr.end();
    }
    return (p >= 0 && p < PAGE_COUNT) ? p : 0;
  }
  if (s == "zerosixty") return RUN_PAGE;
  if (s == "faults") return FAULT_PAGE;
  for (int i = 0; i < METRIC_COUNT; i++) {
    if (s == METRICS[i].key) return i + 1;
  }
  return 0;
}

void uiBuild(lv_obj_t* scr) {
  buildBackground(scr);
  buildHomePage(scr);
  buildMetricPages(scr);
  buildRunPage(scr);
  buildFaultPage(scr);
  buildChrome(scr);
  buildMenu(scr);
  applyTheme();
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, rootEvent, LV_EVENT_ALL, nullptr);
  runTimer = lv_timer_create(runCb, 20, nullptr);
  lv_timer_pause(runTimer);
  faultTimer = lv_timer_create(faultCb, 30, nullptr);
  lv_timer_pause(faultTimer);
  showPage(startPage(), false);
  lv_timer_create(refreshCb, 100, nullptr);
}
