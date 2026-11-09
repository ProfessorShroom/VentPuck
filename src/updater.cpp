// Copyright © Charlie Howard 2026 All rights reserved
#include "updater.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include "mbedtls/sha256.h"
#include "lvgl.h"
#include "settings.h"
#include "display.h"
#include "version.h"
#include "versionCompare.h"

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

static void uiOpen() {
  bool dark = g_settings.darkMode;
  cover = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(cover);
  lv_obj_set_size(cover, 466, 466);
  lv_obj_set_pos(cover, 0, 0);
  lv_obj_set_style_bg_color(cover, dark ? lv_color_hex(0x000000) : lv_color_hex(0x0D0620), 0);
  lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
  lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE);

  titleLbl = lv_label_create(cover);
  lv_obj_set_width(titleLbl, 340);
  lv_obj_set_pos(titleLbl, 63, 120);
  lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(titleLbl, lv_color_hex(0xEAD9FF), 0);
  lv_obj_set_style_text_align(titleLbl, LV_TEXT_ALIGN_CENTER, 0);

  statusLbl = lv_label_create(cover);
  lv_obj_set_width(statusLbl, 340);
  lv_obj_set_pos(statusLbl, 63, 185);
  lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(statusLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(statusLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(statusLbl, LV_LABEL_LONG_WRAP);

  bar = lv_bar_create(cover);
  lv_obj_set_size(bar, 260, 14);
  lv_obj_set_pos(bar, 103, 300);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x2A0A78), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x8A5CFF), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 7, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 7, LV_PART_INDICATOR);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

static void uiClose() {
  if (cover) lv_obj_del(cover);
  cover = nullptr;
  displayLoop();
}

static void uiShow(const char* title, const char* status, int pct) {
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

static bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_settings.ssid.c_str(), g_settings.password.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    displayLoop();
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

static bool syncClock() {
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  uint32_t t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 10000) {
    displayLoop();
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
    Serial.printf("Update: GitHub answered %d (%s)\n", code, HTTPClient::errorToString(code).c_str());
    *error = code < 0 ? "Couldn't reach GitHub" : "GitHub didn't give a release list";
    http.end();
    return false;
  }
  JsonDocument filter;
  filter[0]["tag_name"] = true;
  filter[0]["draft"] = true;
  filter[0]["assets"][0]["name"] = true;
  filter[0]["assets"][0]["browser_download_url"] = true;
  filter[0]["assets"][0]["size"] = true;
  filter[0]["assets"][0]["digest"] = true;
  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (je) {
    Serial.printf("Update: bad release list (%s)\n", je.c_str());
    *error = "Couldn't read the release list";
    return false;
  }
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
    Serial.printf("Update: download answered %d (%s)\n", code, HTTPClient::errorToString(code).c_str());
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
    Serial.printf("Update: begin failed: %s\n", Update.errorString());
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
      Serial.printf("Update: checksum mismatch, got %s expected %s\n", hex, c.sha);
      *error = "The update failed its checksum";
      return false;
    }
  }
  if (!Update.end(true)) {
    Serial.printf("Update: end failed: %s\n", Update.errorString());
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

void updaterBoot() {
  bool forced = takeRequest();
  if (!forced && !g_settings.update) return;
  uiOpen();
  const char* error = nullptr;
  if (g_settings.ssid.length() == 0) {
    if (forced) {
      uiShow("UPDATES", "No Wi-Fi set. Add ssid and password to settings.ini on the SD card.", -1);
      pumpFor(4000);
    }
    uiClose();
    return;
  }
  uiShow("UPDATES", "Connecting to Wi-Fi...", -1);
  if (!connectWifi()) {
    Serial.println("Update: Wi-Fi didn't connect");
    if (forced) {
      uiShow("UPDATES", "Couldn't connect to Wi-Fi", -1);
      pumpFor(2500);
    }
    radioOff();
    uiClose();
    return;
  }
  uiShow("UPDATES", "Checking the time...", -1);
  if (!syncClock()) {
    Serial.println("Update: clock didn't sync");
    uiShow("UPDATES", "Couldn't get the time", -1);
    pumpFor(2500);
    radioOff();
    uiClose();
    return;
  }
  FwVersion current;
  versionParse(FW_VERSION, &current);
  bool beta = g_settings.updateChannel == "beta";
  uiShow("UPDATES", beta ? "Checking for updates (Beta)..." : "Checking for updates...", -1);
  Candidate c;
  memset(&c, 0, sizeof c);
  bool ok = findUpdate(c, beta, current, &error);
  if (!ok) {
    uiShow("UPDATES", error, -1);
    pumpFor(3000);
  } else if (!c.found) {
    Serial.printf("Update: %s is the newest on the %s channel\n", FW_VERSION, beta ? "beta" : "release");
    uiShow("UPDATES", "Up to date", -1);
    pumpFor(forced ? 2000 : 800);
  } else {
    Serial.printf("Update: %s -> %s\n", FW_VERSION, c.tag);
    char line[64];
    snprintf(line, sizeof line, "Version %s", c.tag);
    uiShow("UPDATING", line, 0);
    if (downloadAndFlash(c, &error)) {
      uiShow("UPDATED", "Restarting...", 100);
      pumpFor(1200);
      ESP.restart();
    }
    Serial.printf("Update: failed: %s\n", error);
    uiShow("UPDATE FAILED", error, -1);
    pumpFor(3500);
  }
  radioOff();
  uiClose();
}
