// Copyright © Charlie Howard 2026 All rights reserved
#include "settings.h"
#include "FS.h"
#include "SD_MMC.h"
#include "esp_heap_caps.h"
#include <Preferences.h>

// SD card pins on the Waveshare ESP32-S3-Touch-AMOLED-1.75 (1-bit SD_MMC)
#define SD_CLK_PIN 2
#define SD_CMD_PIN 1
#define SD_D0_PIN 3

Settings g_settings;
bool g_sdOk = false;
uint8_t* g_logoPng = nullptr;
size_t g_logoLen = 0;

static bool mountSd() {
  if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, -1, -1, -1)) {
    Serial.println("SD: pin setup failed, using defaults");
    return false;
  }
  // false: never format a card we can't read
  if (!SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("SD: no card (or unreadable), using defaults");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("SD: no card, using defaults");
    return false;
  }
  Serial.println("SD: card mounted");
  return true;
}

static bool parseBool(String v, bool def) {
  v.trim();
  v.toLowerCase();
  if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
  if (v == "false" || v == "0" || v == "no" || v == "off") return false;
  return def;
}

static bool parseColour(String v, uint32_t& out) {
  v.trim();
  if (v.startsWith("#")) v.remove(0, 1);
  else if (v.startsWith("0x") || v.startsWith("0X")) v.remove(0, 2);
  if (v.length() != 6) return false;
  for (size_t i = 0; i < 6; i++) {
    if (!isxdigit((unsigned char)v[i])) return false;
  }
  out = (uint32_t)strtoul(v.c_str(), nullptr, 16);
  return true;
}

static const char* METRIC_KEYS[] = {"boost", "oil", "coolant", "rpm", "speed", "voltage", "iat", "load", "throttle"};

static bool metricKeyValid(const String& k) {
  for (const char* m : METRIC_KEYS) {
    if (k == m) return true;
  }
  return false;
}

static void parseHomeItems(String val) {
  int slot = 0;
  int start = 0;
  while (slot < 4 && start <= (int)val.length()) {
    int comma = val.indexOf(',', start);
    String item = comma < 0 ? val.substring(start) : val.substring(start, comma);
    item.trim();
    item.toLowerCase();
    if (metricKeyValid(item)) g_settings.homeItems[slot] = item;
    slot++;
    if (comma < 0) break;
    start = comma + 1;
  }
}

static const char* SETTING_KEYS[] = {"update", "ssid", "password", "ssid2", "password2", "ssid3", "password3", "ssid4", "password4", "ssid5", "password5", "colour1", "colour2", "speed_unit", "boost_unit",
                                     "temp_unit", "brightness", "boot_animation", "vehicle", "start_screen", "home_items", "dark_mode", "update_channel", "accent"};

static bool wifiKey(const String& k, int* slot, bool* isPassword) {
  String rest;
  if (k.startsWith("ssid")) {
    *isPassword = false;
    rest = k.substring(4);
  } else if (k.startsWith("password")) {
    *isPassword = true;
    rest = k.substring(8);
  } else {
    return false;
  }
  if (rest.length() == 0) {
    *slot = 0;
    return true;
  }
  if (rest.length() != 1 || rest[0] < '2' || rest[0] > '0' + WIFI_SLOTS) return false;
  *slot = rest[0] - '1';
  return true;
}

static String iniValue(const char* key) {
  String k = key;
  char buf[16];
  if (k == "update") return g_settings.update ? "true" : "false";
  if (k == "update_channel") return g_settings.updateChannel;
  if (k == "accent") return g_settings.accent;
  int wifiSlot;
  bool wifiPw;
  if (wifiKey(k, &wifiSlot, &wifiPw)) return wifiPw ? g_settings.wifiPassword[wifiSlot] : g_settings.wifiSsid[wifiSlot];
  if (k == "colour1") {
    snprintf(buf, sizeof buf, "#%06X", (unsigned)g_settings.colour1);
    return String(buf);
  }
  if (k == "colour2") {
    snprintf(buf, sizeof buf, "#%06X", (unsigned)g_settings.colour2);
    return String(buf);
  }
  if (k == "speed_unit") return g_settings.speedUnit;
  if (k == "boost_unit") return g_settings.boostUnit;
  if (k == "temp_unit") return g_settings.tempUnit;
  if (k == "brightness") return String(g_settings.brightness);
  if (k == "boot_animation") return g_settings.bootAnimation ? "true" : "false";
  if (k == "dark_mode") return g_settings.darkMode ? "true" : "false";
  if (k == "vehicle") return g_settings.vehicle;
  if (k == "start_screen") return g_settings.startScreen;
  if (k == "home_items") {
    return g_settings.homeItems[0] + "," + g_settings.homeItems[1] + "," + g_settings.homeItems[2] + "," +
           g_settings.homeItems[3];
  }
  return "";
}

int wifiCount() {
  int n = 0;
  for (int i = 0; i < WIFI_SLOTS; i++) {
    if (g_settings.wifiSsid[i].length() > 0) n++;
  }
  return n;
}

static void applyKey(String key, String val) {
  key.trim();
  key.toLowerCase();
  val.trim();
  if (val.length() >= 2 && val.startsWith("\"") && val.endsWith("\"")) val = val.substring(1, val.length() - 1);

  int wifiSlot;
  bool wifiPw;
  if (wifiKey(key, &wifiSlot, &wifiPw)) {
    if (wifiPw) g_settings.wifiPassword[wifiSlot] = val;
    else g_settings.wifiSsid[wifiSlot] = val;
    return;
  }

  if (key == "update") {
    g_settings.update = parseBool(val, false);
  } else if (key == "accent") {
    val.toLowerCase();
    static const char* names[] = {"purple", "blue", "cyan", "green", "yellow", "orange", "red", "pink", "white"};
    for (const char* n : names) {
      if (val == n) g_settings.accent = val;
    }
  } else if (key == "update_channel") {
    val.toLowerCase();
    if (val == "release" || val == "beta") g_settings.updateChannel = val;
  } else if (key == "colour1" || key == "color1") {
    if (!parseColour(val, g_settings.colour1)) Serial.printf("Settings: bad colour1 '%s', keeping default\n", val.c_str());
  } else if (key == "colour2" || key == "color2") {
    if (!parseColour(val, g_settings.colour2)) Serial.printf("Settings: bad colour2 '%s', keeping default\n", val.c_str());
  } else if (key == "speed_unit") {
    val.toLowerCase();
    if (val == "mph" || val == "kph") g_settings.speedUnit = val;
  } else if (key == "boost_unit") {
    val.toLowerCase();
    if (val == "bar" || val == "psi" || val == "kpa") g_settings.boostUnit = val;
  } else if (key == "temp_unit") {
    val.toUpperCase();
    if (val == "C" || val == "F") g_settings.tempUnit = val;
  } else if (key == "brightness") {
    int b = val.toInt();
    g_settings.brightness = b < 5 ? 5 : (b > 100 ? 100 : b);
  } else if (key == "boot_animation") {
    g_settings.bootAnimation = parseBool(val, true);
  } else if (key == "dark_mode") {
    g_settings.darkMode = parseBool(val, false);
  } else if (key == "vehicle") {
    val.toLowerCase();
    if (val == "renault") val = "meganers3";
    if (val == "generic" || val == "meganers3" || val == "clio4rs" || val == "megane4rs" || val == "bmwe8x") g_settings.vehicle = val;
  } else if (key == "start_screen") {
    val.toLowerCase();
    if (val == "last" || val == "home" || val == "zerosixty" || val == "faults" || val == "temps" || metricKeyValid(val)) g_settings.startScreen = val;
  } else if (key == "home_items") {
    parseHomeItems(val);
  } else if (key.length()) {
    Serial.printf("Settings: unknown key '%s' ignored\n", key.c_str());
  }
}

static void loadSettingsFile() {
  File f = SD_MMC.open("/settings.ini", FILE_READ);
  if (!f) {
    Serial.println("Settings: couldn't open settings.ini, using defaults");
    return;
  }
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.replace("\r", "");
    line.trim();
    if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) continue;
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    applyKey(line.substring(0, eq), line.substring(eq + 1));
  }
  f.close();
  Serial.println("Settings: loaded settings.ini");
}

static bool writeIni() {
  File f = SD_MMC.open("/settings.ini", FILE_WRITE);
  if (!f) return false;
  f.print("# VentPuck settings. All optional, and missing values use the defaults.\n");
  f.print("# Comments must be on their own line, because hex colours start with #.\n");
  f.print("# These can also be changed from the on-screen menu: press and hold on any screen.\n\n");
  f.print("# Vehicle: meganers3 (Renault Megane RS Mk3), clio4rs (Renault Clio RS Mk4), megane4rs (Renault Megane RS Mk4) - all three add engine ECU readings such as oil temp, bmwe8x (BMW 1 Series E8x, standard OBD for now) or generic (standard OBD only)\n");
  f.printf("vehicle=%s\n\n", iniValue("vehicle").c_str());
  f.print("# Updates (Wi-Fi is only used to check for updates). update_channel is release or beta, and beta builds may be unstable\n");
  f.printf("update=%s\n", iniValue("update").c_str());
  f.printf("update_channel=%s\n", iniValue("update_channel").c_str());
  f.print("# Up to 5 Wi-Fi networks, and it joins the strongest one it can see\n");
  f.printf("ssid=%s\n", iniValue("ssid").c_str());
  f.printf("password=%s\n", iniValue("password").c_str());
  for (int i = 2; i <= WIFI_SLOTS; i++) {
    char key[16];
    snprintf(key, sizeof key, "ssid%d", i);
    f.printf("%s=%s\n", key, iniValue(key).c_str());
    snprintf(key, sizeof key, "password%d", i);
    f.printf("%s=%s\n", key, iniValue(key).c_str());
  }
  f.print("\n");
  f.print("# Dial gradient as hex #RRGGBB: colour1 = outer edge, colour2 = middle\n");
  f.printf("colour1=%s\n", iniValue("colour1").c_str());
  f.printf("colour2=%s\n\n", iniValue("colour2").c_str());
  f.print("# Units: speed mph or kph, boost bar or psi or kpa, temp C or F\n");
  f.printf("speed_unit=%s\n", iniValue("speed_unit").c_str());
  f.printf("boost_unit=%s\n", iniValue("boost_unit").c_str());
  f.printf("temp_unit=%s\n\n", iniValue("temp_unit").c_str());
  f.print("# Screens: start_screen is last, home, boost, temps, rpm, speed, voltage, iat, load, zerosixty or faults\n");
  f.print("# home_items are the four readings on the home screen, from boost, oil, coolant, rpm, speed, voltage, iat, load, throttle\n");
  f.printf("start_screen=%s\n", iniValue("start_screen").c_str());
  f.printf("home_items=%s\n\n", iniValue("home_items").c_str());
  f.print("# Display\n");
  f.printf("brightness=%s\n", iniValue("brightness").c_str());
  f.printf("boot_animation=%s\n", iniValue("boot_animation").c_str());
  f.print("# Dark mode: true makes the background black, which saves power and looks best on the AMOLED\n");
  f.printf("dark_mode=%s\n", iniValue("dark_mode").c_str());
  f.print("# Accent colour used in dark mode: purple, blue, cyan, green, yellow, orange, red, pink or white\n");
  f.printf("accent=%s\n", iniValue("accent").c_str());
  f.close();
  return true;
}

static void nvsLoad() {
  Preferences p;
  if (!p.begin("ventpuck", true)) return;
  for (const char* k : SETTING_KEYS) {
    if (p.isKey(k)) applyKey(k, p.getString(k));
  }
  p.end();
}

void settingsSave() {
  Preferences p;
  if (p.begin("ventpuck", false)) {
    for (const char* k : SETTING_KEYS) p.putString(k, iniValue(k));
    p.end();
  }
  if (g_sdOk) {
    if (writeIni()) Serial.println("Settings: saved");
    else Serial.println("Settings: couldn't write settings.ini");
  }
}

static uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void loadLogo() {
  if (!SD_MMC.exists("/logo.png")) return;
  File f = SD_MMC.open("/logo.png", FILE_READ);
  if (!f) return;
  size_t n = f.size();
  if (n < 64 || n > 1024 * 1024) {
    Serial.printf("Logo: /logo.png is %u bytes, must be between 64 bytes and 1 MB, using built-in logo\n", (unsigned)n);
    f.close();
    return;
  }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) {
    Serial.println("Logo: out of memory, using built-in logo");
    f.close();
    return;
  }
  size_t got = f.read(buf, n);
  f.close();
  static const uint8_t magic[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
  if (got != n || memcmp(buf, magic, 8) != 0) {
    Serial.println("Logo: /logo.png isn't a valid PNG, using built-in logo");
    heap_caps_free(buf);
    return;
  }
  uint32_t w = be32(buf + 16), h = be32(buf + 20);
  if (w == 0 || h == 0 || w > 1024 || h > 1024) {
    Serial.printf("Logo: /logo.png is %ux%u, max 1024x1024, using built-in logo\n", (unsigned)w, (unsigned)h);
    heap_caps_free(buf);
    return;
  }
  g_logoPng = buf;
  g_logoLen = n;
  Serial.printf("Logo: using /logo.png (%ux%u, %u bytes)\n", (unsigned)w, (unsigned)h, (unsigned)n);
}

void settingsInit() {
  nvsLoad();
  g_sdOk = mountSd();
  if (g_sdOk) {
    if (SD_MMC.exists("/settings.ini")) loadSettingsFile();
    else if (writeIni()) Serial.println("Settings: no settings.ini found, created one on the card");
    loadLogo();
  }
  Serial.printf("Settings: vehicle=%s update=%d wifi=%d colour1=#%06X colour2=#%06X units=%s/%s/%s brightness=%d boot_animation=%d\n",
                g_settings.vehicle.c_str(), g_settings.update, wifiCount(), (unsigned)g_settings.colour1,
                (unsigned)g_settings.colour2, g_settings.speedUnit.c_str(), g_settings.boostUnit.c_str(),
                g_settings.tempUnit.c_str(), g_settings.brightness, g_settings.bootAnimation);
  Serial.printf("Settings: start_screen=%s home_items=%s\n", g_settings.startScreen.c_str(), iniValue("home_items").c_str());
}
