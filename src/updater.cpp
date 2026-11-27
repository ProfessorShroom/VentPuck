// Copyright © Charlie Howard 2026 All rights reserved
#include "updater.h"
#include "sdlog.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include "mbedtls/sha256.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "settings.h"
#include "display.h"
#include "version.h"
#include "versionCompare.h"
#include "obd.h"
#include "ui.h"

extern const uint8_t certBundleStart[] asm("_binary_certs_x509_crt_bundle_bin_start");
extern const uint8_t certBundleEnd[] asm("_binary_certs_x509_crt_bundle_bin_end");

static const char* RELEASES_URL = "https://api.github.com/repos/ProfessorShroom/VentPuck/releases?per_page=20";
static const char* FIRMWARE_ASSET = "firmware.bin";

struct Candidate {
  char tag[32];
  char url[256];
  char sha[72];
  size_t size;
  FwVersion ver;
  bool found;
};

static lv_obj_t* cover = nullptr;
static lv_obj_t* titleLbl = nullptr;
static lv_obj_t* statusLbl = nullptr;
static lv_obj_t* bar = nullptr;
static bool quiet = false;

static void uiOpen() {
  ThemeColours theme = uiTheme();
  cover = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(cover);
  lv_obj_set_size(cover, 466, 466);
  lv_obj_set_pos(cover, 0, 0);
  lv_obj_set_style_bg_color(cover, lv_color_hex(theme.menuBg), 0);
  lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
  lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE);

  titleLbl = lv_label_create(cover);
  lv_obj_set_width(titleLbl, 340);
  lv_obj_set_pos(titleLbl, 63, 120);
  lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(titleLbl, lv_color_hex(theme.icon), 0);
  lv_obj_set_style_text_align(titleLbl, LV_TEXT_ALIGN_CENTER, 0);

  statusLbl = lv_label_create(cover);
  lv_obj_set_width(statusLbl, 340);
  lv_obj_set_pos(statusLbl, 63, 185);
  lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(statusLbl, lv_color_hex(theme.value), 0);
  lv_obj_set_style_text_align(statusLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(statusLbl, LV_LABEL_LONG_WRAP);

  bar = lv_bar_create(cover);
  lv_obj_set_size(bar, 260, 14);
  lv_obj_set_pos(bar, 103, 300);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_bg_color(bar, lv_color_hex(theme.btnDark), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(theme.btnPressed), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 7, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 7, LV_PART_INDICATOR);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

static void uiClose() {
  if (quiet) return;
  if (cover) lv_obj_del(cover);
  cover = nullptr;
  displayLoop();
}

static void uiShow(const char* title, const char* status, int pct) {
  if (quiet) return;
  if (!cover) {
    displayLoop();
    return;
  }
  lv_label_set_text(titleLbl, title);
  lv_label_set_text(statusLbl, status);
  if (pct >= 0) {
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
  } else {
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
  }
  displayLoop();
}

static void pumpFor(uint32_t ms) {
  if (!cover) return;
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    displayLoop();
    delay(10);
  }
}

static void radioOff() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

static bool connectWifi(uint32_t perNetworkMs) {
  WiFi.mode(WIFI_STA);
  WiFiMulti multi;
  for (int i = 0; i < WIFI_SLOTS; i++) {
    if (g_settings.wifiSsid[i].length() > 0) multi.addAP(g_settings.wifiSsid[i].c_str(), g_settings.wifiPassword[i].c_str());
  }
  bool ok = multi.run(perNetworkMs) == WL_CONNECTED;
  if (ok) Log.printf("Update: joined '%s'\n", WiFi.SSID().c_str());
  return ok;
}

static bool syncClock() {
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  uint32_t t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 10000) {
    if (!quiet) displayLoop();
    delay(50);
  }
  return time(nullptr) >= 1700000000;
}

static void configureTls(NetworkClientSecure& client) {
  client.setCACertBundle(certBundleStart, certBundleEnd - certBundleStart);
}

static bool findUpdate(Candidate& best, bool betaChannel, const FwVersion& current, const char** error) {
  best.found = false;
  NetworkClientSecure client;
  configureTls(client);
  HTTPClient http;
  http.setTimeout(15000);
  http.useHTTP10(true);
  http.setUserAgent("VentPuck/" FW_VERSION);
  if (!http.begin(client, RELEASES_URL)) {
    *error = "Couldn't start the request";
    return false;
  }
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != 200) {
    Log.printf("Update: GitHub answered %d (%s)\n", code, HTTPClient::errorToString(code).c_str());
    *error = code < 0 ? "Couldn't reach GitHub" : "GitHub didn't give a release list";
    http.end();
    return false;
  }
  int bodyLen = http.getSize();
  const size_t cap = 262144;
  uint8_t* body = (uint8_t*)heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!body) {
    http.end();
    *error = "Out of memory";
    return false;
  }
  NetworkClient* stream = http.getStreamPtr();
  size_t got = 0;
  uint32_t lastData = millis();
  while (got < cap) {
    size_t avail = stream->available();
    if (avail) {
      size_t want = cap - got;
      size_t n = stream->readBytes(body + got, avail > want ? want : avail);
      got += n;
      lastData = millis();
      if (bodyLen > 0 && got >= (size_t)bodyLen) break;
    } else {
      if (!stream->connected()) break;
      if (millis() - lastData > 10000) break;
      delay(5);
    }
  }
  http.end();
  body[got] = 0;
  Log.printf("Update: release list is %u bytes (server said %d)\n", (unsigned)got, bodyLen);
  JsonDocument filter;
  filter[0]["tag_name"] = true;
  filter[0]["draft"] = true;
  filter[0]["assets"][0]["name"] = true;
  filter[0]["assets"][0]["browser_download_url"] = true;
  filter[0]["assets"][0]["size"] = true;
  filter[0]["assets"][0]["digest"] = true;
  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, (const uint8_t*)body, got, DeserializationOption::Filter(filter));
  if (je) {
    Log.printf("Update: bad release list (%s), starts: %.60s\n", je.c_str(), (const char*)body);
    heap_caps_free(body);
    *error = "Couldn't read the release list";
    return false;
  }
  heap_caps_free(body);
  for (JsonObject rel : doc.as<JsonArray>()) {
    if (rel["draft"] | false) continue;
    const char* tag = rel["tag_name"] | "";
    FwVersion v;
    if (!versionParse(tag, &v)) continue;
    if (versionIsBeta(v) && !betaChannel) continue;
    const char* url = nullptr;
    const char* digest = "";
    size_t size = 0;
    for (JsonObject a : rel["assets"].as<JsonArray>()) {
      const char* name = a["name"] | "";
      if (strcmp(name, FIRMWARE_ASSET) == 0) {
        url = a["browser_download_url"] | (const char*)nullptr;
        digest = a["digest"] | "";
        size = a["size"] | 0;
        break;
      }
    }
    if (!url) continue;
    if (best.found && versionCompare(v, best.ver) <= 0) continue;
    snprintf(best.tag, sizeof best.tag, "%s", tag);
    snprintf(best.url, sizeof best.url, "%s", url);
    snprintf(best.sha, sizeof best.sha, "%s", strncmp(digest, "sha256:", 7) == 0 ? digest + 7 : "");
    best.size = size;
    best.ver = v;
    best.found = true;
  }
  if (best.found && versionCompare(best.ver, current) <= 0) best.found = false;
  return true;
}

static void hexOf(const uint8_t* in, size_t n, char* out) {
  for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", in[i]);
}

static bool downloadAndFlash(const Candidate& c, const char** error) {
  NetworkClientSecure client;
  configureTls(client);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  http.useHTTP10(true);
  http.setUserAgent("VentPuck/" FW_VERSION);
  if (!http.begin(client, c.url)) {
    *error = "Couldn't start the download";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    Log.printf("Update: download answered %d (%s)\n", code, HTTPClient::errorToString(code).c_str());
    *error = "Couldn't download the update";
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len <= 0 || (c.size && (size_t)len != c.size)) {
    *error = "The update file looks wrong";
    http.end();
    return false;
  }
  if (!Update.begin(len)) {
    Log.printf("Update: begin failed: %s\n", Update.errorString());
    *error = "Not enough room for the update";
    http.end();
    return false;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  NetworkClient* stream = http.getStreamPtr();
  static uint8_t buf[2048];
  size_t got = 0;
  uint32_t lastData = millis();
  uint32_t lastUi = 0;
  bool writeFailed = false;
  while (got < (size_t)len) {
    size_t avail = stream->available();
    if (avail) {
      size_t n = stream->readBytes(buf, avail > sizeof buf ? sizeof buf : avail);
      if (Update.write(buf, n) != n) {
        writeFailed = true;
        break;
      }
      mbedtls_sha256_update(&sha, buf, n);
      got += n;
      lastData = millis();
    } else {
      if (!stream->connected()) break;
      if (millis() - lastData > 20000) break;
      delay(2);
    }
    if (millis() - lastUi > 100) {
      lastUi = millis();
      char line[48];
      snprintf(line, sizeof line, "%u of %u KB", (unsigned)(got / 1024), (unsigned)(len / 1024));
      uiShow("UPDATING", line, (int)((uint64_t)got * 100 / len));
    }
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (writeFailed || got != (size_t)len) {
    Update.abort();
    *error = writeFailed ? "Couldn't write the update" : "The download was cut short";
    return false;
  }
  if (c.sha[0]) {
    char hex[65];
    hexOf(digest, 32, hex);
    if (strcasecmp(hex, c.sha) != 0) {
      Update.abort();
      Log.printf("Update: checksum mismatch, got %s expected %s\n", hex, c.sha);
      *error = "The update failed its checksum";
      return false;
    }
  }
  if (!Update.end(true)) {
    Log.printf("Update: end failed: %s\n", Update.errorString());
    *error = "The update wasn't valid";
    return false;
  }
  return true;
}

void updaterRequestNow() {
  Preferences p;
  if (p.begin("ventpuck", false)) {
    p.putBool("updateNow", true);
    p.end();
  }
}

static bool takeRequest() {
  bool now = false;
  Preferences p;
  if (p.begin("ventpuck", false)) {
    now = p.getBool("updateNow", false);
    if (now) p.putBool("updateNow", false);
    p.end();
  }
  return now;
}

static void checkAndInstall(bool forced) {
  if (forced) uiOpen();
  const char* error = nullptr;
  if (wifiCount() == 0) {
    if (forced) {
      uiShow("UPDATES", "No Wi-Fi set. Add ssid and password to settings.ini on the SD card.", -1);
      pumpFor(4000);
    }
    uiClose();
    return;
  }
  Log.printf("Update: running %s, channel %s, %d Wi-Fi network(s) set\n", FW_VERSION, g_settings.updateChannel.c_str(),
                wifiCount());
  uiShow("UPDATES", "Connecting to Wi-Fi...", -1);
  bool joined = false;
  for (int attempt = 0; attempt < (forced ? 1 : 3) && !joined; attempt++) {
    if (attempt) {
      Log.printf("Update: Wi-Fi didn't connect, retrying (%d of 3)\n", attempt + 1);
      radioOff();
      delay(2000);
    }
    joined = connectWifi(forced ? 12000 : 15000);
  }
  if (!joined) {
    Log.printf("Update: Wi-Fi didn't connect (status %d)\n", (int)WiFi.status());
    if (forced) {
      uiShow("UPDATES", "Couldn't connect to Wi-Fi", -1);
      pumpFor(2500);
    }
    radioOff();
    uiClose();
    return;
  }
  Log.println("Update: Wi-Fi connected, syncing the clock");
  uiShow("UPDATES", "Checking the time...", -1);
  if (!syncClock()) {
    Log.println("Update: clock didn't sync");
    if (forced) {
      uiShow("UPDATES", "Couldn't get the time", -1);
      pumpFor(2500);
    }
    radioOff();
    uiClose();
    return;
  }
  Log.printf("Update: clock set, reading the release list (free internal heap %u, largest block %u)\n",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  FwVersion current;
  versionParse(FW_VERSION, &current);
  bool beta = g_settings.updateChannel == "beta";
  uiShow("UPDATES", beta ? "Checking for updates (Beta)..." : "Checking for updates...", -1);
  Candidate c;
  memset(&c, 0, sizeof c);
  bool ok = findUpdate(c, beta, current, &error);
  if (!ok) {
    if (forced) {
      uiShow("UPDATES", error, -1);
      pumpFor(3000);
    }
  } else if (!c.found) {
    Log.printf("Update: %s is the newest on the %s channel\n", FW_VERSION, beta ? "beta" : "release");
    if (forced) {
      uiShow("UPDATES", "Up to date", -1);
      pumpFor(2000);
    }
  } else {
    Log.printf("Update: %s -> %s\n", FW_VERSION, c.tag);
    if (!quiet && !cover) uiOpen();
    char line[64];
    snprintf(line, sizeof line, "Version %s", c.tag);
    uiShow("UPDATING", line, 0);
    if (downloadAndFlash(c, &error)) {
      if (quiet) {
        Log.printf("Update: %s installed, it runs the next time VentPuck starts\n", c.tag);
        radioOff();
        return;
      }
      uiShow("UPDATED", "Restarting...", 100);
      pumpFor(1200);
      ESP.restart();
    }
    Log.printf("Update: failed: %s\n", error);
    uiShow("UPDATE FAILED", error, -1);
    pumpFor(3500);
  }
  radioOff();
  uiClose();
}

static bool checkedThisBoot = false;

void updaterBoot() {
  if (!takeRequest()) return;
  checkedThisBoot = true;
  checkAndInstall(true);
}

static void backgroundTask(void*) {
  uint32_t t0 = millis();
  while (g_live.state != OBD_LIVE && millis() - t0 < 30000) delay(500);
  quiet = true;
  bool paused = g_live.state == OBD_LIVE;
  if (paused) {
    Log.println("Update: pausing car readings for the update check");
    obdPause(true);
  }
  checkAndInstall(false);
  if (paused) {
    obdPause(false);
    Log.println("Update: car readings resumed");
  }
  quiet = false;
  vTaskDelete(nullptr);
}

void updaterStartBackground() {
  if (checkedThisBoot || !g_settings.update || wifiCount() == 0) return;
  xTaskCreatePinnedToCore(backgroundTask, "update", 12288, nullptr, 1, nullptr, 1);
}
