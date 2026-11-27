// Copyright © Charlie Howard 2026 All rights reserved

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "obd.h"
#include "sdlog.h"
#include "settings.h"
#include "dtc.h"
#include <Preferences.h>
#include <math.h>
#include <esp_task_wdt.h>

static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* txChar = nullptr;  // FFF2 (write)
static NimBLERemoteCharacteristic* rxChar = nullptr;  // FFF1 (notify)

static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
static char rxBuf[1024];
static size_t rxLen = 0;
static volatile bool gotPrompt = false;
static bool lastTimedOut = false;
static bool pollEnabled = true;
static volatile bool pauseRequested = false;
static volatile bool pauseActive = false;
static bool supported[256] = {false};
static bool worked[256] = {false};
static bool renaultOil = false;
static bool renaultBoost = false;
static bool renaultBaro = false;
static bool renaultVolt = false;
static bool boostAbsolute = true;
static float renaultBaroMbar = NAN;
static bool bmwOil = false;
static int bmwVariant = 0;
static int bmwFormula = 0;

static const uint32_t STALE_MS = 10000;
static const uint32_t WDT_MS = 60000;

struct Tracked {
  volatile float* value;
  uint32_t at;
};

static Tracked tracked[] = {
    {&g_live.rpm, 0},    {&g_live.speedKph, 0}, {&g_live.coolantC, 0},    {&g_live.oilC, 0},
    {&g_live.mapKpa, 0}, {&g_live.batteryV, 0}, {&g_live.iatC, 0},        {&g_live.loadPct, 0},
    {&g_live.throttlePct, 0}, {&g_live.ambientC, 0}};

static void put(volatile float& f, float v) {
  f = v;
  for (Tracked& t : tracked) {
    if (t.value == &f) t.at = millis();
  }
}

static void expireStale() {
  uint32_t now = millis();
  for (Tracked& t : tracked) {
    if (!isnan(*t.value) && now - t.at > STALE_MS) *t.value = NAN;
  }
}

static void clearLive() {
  for (Tracked& t : tracked) *t.value = NAN;
  g_live.baroKpa = NAN;
}

struct KeyPid {
  uint8_t pid;
  const char* name;
  const char* unit;
};

static const KeyPid KEYS[] = {
    {0x0C, "RPM", "rpm"},
    {0x0D, "Speed", "km/h"},
    {0x05, "Coolant", "C"},
    {0x5C, "Oil temp", "C"},
    {0x0B, "MAP", "kPa"},
    {0x33, "Baro", "kPa"},
    {0x0F, "Intake air", "C"},
    {0x42, "Module voltage", "V"},
    {0x04, "Engine load", "%"},
    {0x11, "Throttle", "%"},
    {0x06, "Short fuel trim", "%"},
    {0x07, "Long fuel trim", "%"},
    {0x46, "Ambient", "C"},
    {0x2F, "Fuel level", "%"},
    {0x14, "O2 B1S1 volts", "V"},
    {0x15, "O2 B1S2 volts", "V"},
    {0x24, "Lambda B1S1 (24)", ""},
    {0x25, "Lambda B1S2 (25)", ""},
    {0x34, "Lambda B1S1 (34)", ""},
    {0x35, "Lambda B1S2 (35)", ""},
};

static int decimals(const KeyPid& k) {
  switch (k.pid) {
    case 0x14:
    case 0x15:
    case 0x24:
    case 0x25:
    case 0x34:
    case 0x35:
    case 0x42:
      return 2;
    default:
      return 1;
  }
}

static void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  portENTER_CRITICAL(&rxMux);
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '>') {
      gotPrompt = true;
    } else if (rxLen < sizeof rxBuf - 1) {
      rxBuf[rxLen++] = c;
    }
  }
  portEXIT_CRITICAL(&rxMux);
}

static void rxClear() {
  portENTER_CRITICAL(&rxMux);
  rxLen = 0;
  gotPrompt = false;
  portEXIT_CRITICAL(&rxMux);
}

static String rxTake() {
  static char copy[sizeof rxBuf];
  portENTER_CRITICAL(&rxMux);
  memcpy(copy, rxBuf, rxLen);
  copy[rxLen] = 0;
  portEXIT_CRITICAL(&rxMux);
  return String(copy);
}

static bool uuidHas(const NimBLEUUID& u, const char* fragment) {
  String s = u.toString().c_str();
  s.toLowerCase();
  return s.indexOf(fragment) >= 0;
}

static bool advertisesSerial(const NimBLEAdvertisedDevice* d) {
  return d->isAdvertisingService(NimBLEUUID((uint16_t)0xFFF0)) || d->isAdvertisingService(NimBLEUUID((uint16_t)0xFFE0));
}

static bool looksLikeObd(const NimBLEAdvertisedDevice* d, const String& lowerName) {
  return lowerName.indexOf("obd") >= 0 || lowerName.indexOf("elm") >= 0 || lowerName.indexOf("vlink") >= 0 ||
         lowerName.indexOf("vgate") >= 0 || advertisesSerial(d);
}

static bool connectAdapter() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  String want = g_settings.adapterName;
  want.trim();
  want.toLowerCase();
  if (want.length()) Log.printf("Scanning for an adapter named '%s' (8 s)...\n", g_settings.adapterName.c_str());
  else Log.println("Scanning for any BLE OBD adapter (8 s)...");
  NimBLEScanResults results = scan->getResults(8000, false);
  esp_task_wdt_reset();

  const NimBLEAdvertisedDevice* target = nullptr;
  for (uint32_t i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    String name = d->getName().c_str();
    Log.printf("  found: %s [%s] type %u RSSI %d%s\n", name.length() ? name.c_str() : "(no name)",
                  d->getAddress().toString().c_str(), d->getAddress().getType(), d->getRSSI(),
                  advertisesSerial(d) ? " serial" : "");
    if (target) continue;
    name.toLowerCase();
    if (want.length() ? name.indexOf(want) >= 0 : looksLikeObd(d, name)) target = d;
  }
  if (!target) {
    Log.println("No adapter found. Is it plugged in, with the ignition on?");
    return false;
  }

  String targetAddr = target->getAddress().toString().c_str();
  if (targetAddr == "00:00:00:00:00:00") {
    Log.printf("Adapter address resolves to zero (stored bonds: %d), clearing bonds\n", NimBLEDevice::getNumBonds());
    NimBLEDevice::deleteAllBonds();
    return false;
  }

  if (client) {
    if (client->isConnected()) client->disconnect();
    NimBLEDevice::deleteClient(client);
    client = nullptr;
  }
  rxChar = nullptr;
  txChar = nullptr;
  client = NimBLEDevice::createClient();
  if (!client) {
    Log.println("Couldn't create a BLE client.");
    return false;
  }
  Log.printf("Connecting to %s (type %u)...\n", targetAddr.c_str(), target->getAddress().getType());
  if (!client->connect(target)) {
    Log.println("Connection failed.");
    return false;
  }
  esp_task_wdt_reset();
  Log.printf("Connected, MTU %u\n", client->getMTU());

  Log.printf("Secure connection: %s\n", client->secureConnection() ? "ok" : "not established");

  esp_task_wdt_reset();
  Log.println("Services:");
  for (auto* svc : client->getServices(true)) {
    Log.printf("  service %s\n", svc->getUUID().toString().c_str());
    for (auto* chr : svc->getCharacteristics(true)) {
      Log.printf("    char %s%s%s%s\n", chr->getUUID().toString().c_str(),
                    chr->canNotify() ? " notify" : "", chr->canWrite() ? " write" : "",
                    chr->canWriteNoResponse() ? " writeNR" : "");
      if (uuidHas(svc->getUUID(), "fff0")) {
        if (uuidHas(chr->getUUID(), "fff1")) rxChar = chr;
        if (uuidHas(chr->getUUID(), "fff2")) txChar = chr;
      } else if (uuidHas(svc->getUUID(), "ffe0") && uuidHas(chr->getUUID(), "ffe1") && !rxChar && !txChar) {
        rxChar = chr;
        txChar = chr;
      }
    }
  }

  if (!rxChar || !txChar) {
    Log.println("Couldn't find a serial service (FFF0 with FFF1/FFF2, or FFE0 with FFE1).");
    return false;
  }
  if (!rxChar->subscribe(true, onNotify)) {
    Log.println("Couldn't subscribe to notifications.");
    return false;
  }
  Log.println("Subscribed, link ready.\n");
  return true;
}

static bool bleWrite(const String& s) {
  size_t mtu = client->getMTU();
  size_t chunk = mtu > 3 ? mtu - 3 : 20;
  for (size_t i = 0; i < s.length(); i += chunk) {
    size_t end = i + chunk;
    if (end > s.length()) end = s.length();
    String part = s.substring(i, end);
    if (!txChar->writeValue((const uint8_t*)part.c_str(), part.length(), txChar->canWrite())) return false;
  }
  return true;
}

static String cmd(const String& c, uint32_t timeoutMs = 5000) {
  if (lastTimedOut) {
    uint32_t t0 = millis();
    while (!gotPrompt && millis() - t0 < 300) delay(5);
  }
  rxClear();
  if (!client || !txChar || !bleWrite(c + "\r")) return "WRITE FAILED";
  uint32_t start = millis();
  while (!gotPrompt && millis() - start < timeoutMs) delay(5);
  esp_task_wdt_reset();
  bool ok = gotPrompt;
  lastTimedOut = !ok;
  String r = rxTake();
  r.trim();
  r.replace("\r", "|");
  return ok ? r : r + " [TIMEOUT]";
}

static int readPid(uint8_t pid, uint8_t* out, int maxLen) {
  char req[8];
  snprintf(req, sizeof req, "01%02X1", pid);
  String r = cmd(req);
  char hdr[6];
  snprintf(hdr, sizeof hdr, "41%02X", pid);
  int idx = r.indexOf(hdr);
  if (idx < 0) return -1;
  int n = 0;
  for (int i = idx + 4; i + 1 < (int)r.length() && n < maxLen; i += 2) {
    if (!isxdigit(r[i]) || !isxdigit(r[i + 1])) break;
    out[n++] = (uint8_t)strtol(r.substring(i, i + 2).c_str(), nullptr, 16);
  }
  return n;
}

static bool decodePid(uint8_t pid, const uint8_t* d, int n, float& v) {
  if (n < 1) return false;
  switch (pid) {
    case 0x04:  // engine load
    case 0x11:  // throttle
    case 0x2F:  // fuel level
      v = d[0] * 100.0f / 255.0f;
      return true;
    case 0x05:  // coolant
    case 0x0F:  // intake air
    case 0x46:  // ambient
    case 0x5C:  // oil temp
      v = d[0] - 40.0f;
      return true;
    case 0x06:  // short fuel trim
    case 0x07:  // long fuel trim
      v = (d[0] - 128.0f) * 100.0f / 128.0f;
      return true;
    case 0x0B:  // MAP
    case 0x33:  // barometric
    case 0x0D:  // speed
      v = d[0];
      return true;
    case 0x0C:  // RPM
      if (n < 2) return false;
      v = (d[0] * 256 + d[1]) / 4.0f;
      return true;
    case 0x14:  // O2 sensor voltage (narrowband)
    case 0x15:
      v = d[0] / 200.0f;
      return true;
    case 0x24:
    case 0x25:
    case 0x34:
    case 0x35:
      if (n < 2) return false;
      v = (d[0] * 256 + d[1]) * 2.0f / 65536.0f;
      return true;
    case 0x42:  // control module voltage
      if (n < 2) return false;
      v = (d[0] * 256 + d[1]) / 1000.0f;
      return true;
    default:
      return false;
  }
}

static bool readValue(uint8_t pid, float& v) {
  uint8_t d[8];
  int n = readPid(pid, d, sizeof d);
  return n > 0 && decodePid(pid, d, n, v);
}

static int readDid(uint16_t did, uint8_t* out, int maxLen) {
  char req[12];
  snprintf(req, sizeof req, "22%04X1", did);
  String r = cmd(req);
  char hdr[8];
  snprintf(hdr, sizeof hdr, "62%04X", did);
  int idx = r.indexOf(hdr);
  if (idx < 0) return -1;
  int n = 0;
  for (int i = idx + 6; i + 1 < (int)r.length() && n < maxLen; i += 2) {
    if (!isxdigit(r[i]) || !isxdigit(r[i + 1])) break;
    out[n++] = (uint8_t)strtol(r.substring(i, i + 2).c_str(), nullptr, 16);
  }
  return n;
}

static bool readDidValue(uint16_t did, float& v) {
  uint8_t d[4];
  if (readDid(did, d, 4) < 2) return false;
  uint16_t raw = ((uint16_t)d[0] << 8) | d[1];
  switch (did) {
    case 0x2007:
      v = ((float)raw - 2730.0f) / 10.0f;
      return true;
    case 0x2005:
      v = raw / 100.0f;
      return true;
    case 0x2009:
    case 0x2401:
    case 0x240B:
    case 0x2402:
      v = raw;
      return true;
    default:
      return false;
  }
}

static void initAdapter() {
  Log.println("--- Initialising adapter ---");
  const char* init[] = {"ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* c : init) {
    String r = cmd(c, c == init[0] ? 6000 : 3000);
    Log.printf("%s -> %s\n", c, r.c_str());
    if (c == init[0]) delay(300);
  }
  const char* info[] = {"ATI", "STI", "STDI", "ATRV"};
  for (const char* c : info) {
    Log.printf("%s -> %s\n", c, cmd(c, 3000).c_str());
  }
}

static void speedRate(uint32_t ms) {
  uint32_t start = millis();
  int n = 0;
  uint8_t d[4];
  while (millis() - start < ms) {
    if (readPid(0x0D, d, 4) > 0) n++;
  }
  float secs = (millis() - start) / 1000.0f;
  Log.printf("Speed-only polling: %d reads in %.1f s = %.1f reads/s\n", n, secs, n / secs);
}

static void summaryLine(const char* screen, bool ok, const char* detail) {
  Log.printf("  %-10s %s  %s\n", screen, ok ? "OK    " : "FAILED", detail);
}

static bool testDid(uint16_t did, const char* name, const char* unit, float& out) {
  bool ok = readDidValue(did, out);
  if (ok) Log.printf("  %-24s (22%04X): %.1f %s\n", name, did, out, unit);
  else Log.printf("  %-24s (22%04X): no reply\n", name, did);
  return ok;
}

static void probeRenault() {
  Log.println("\n--- Renault engine ECU requests (service 22 to 7E0) ---");
  cmd("ATSH7E0", 3000);
  float v;
  float boostMbar = NAN;
  renaultOil = testDid(0x2007, "Oil temp", "C", v);
  if (renaultOil) put(g_live.oilC, v);
  renaultBaro = testDid(0x2009, "Atmospheric pressure", "mbar", v);
  if (renaultBaro) renaultBaroMbar = v;
  renaultBoost = testDid(0x2401, "Boost pressure", "mbar", boostMbar);
  if (renaultBoost) {
    boostAbsolute = boostMbar > 600.0f;
    Log.printf("  Boost pressure looks %s\n", boostAbsolute ? "absolute (about atmospheric at idle)" : "relative (about zero at idle)");
  }
  renaultVolt = testDid(0x2005, "Battery voltage", "V", v);
  if (renaultVolt) put(g_live.batteryV, v);
  float extra;
  testDid(0x240B, "Intake manifold pressure", "mbar", extra);
  testDid(0x2402, "Boost setpoint", "hPa", extra);
  if (!renaultOil && !renaultBoost && !renaultBaro && !renaultVolt) {
    Log.println("  No replies, going back to standard OBD addressing");
    cmd("ATSH7DF", 3000);
  }
}

static bool bmwParse(const String& reply, bool raw, uint8_t* d, int* n) {
  String s = reply;
  s.replace(" ", "");
  int start = 0;
  while (start <= (int)s.length()) {
    int end = s.indexOf('|', start);
    if (end < 0) end = s.length();
    String line = s.substring(start, end);
    int idx = line.indexOf("624402");
    if (idx >= 0) {
      int bytesAfter = ((int)line.length() - (idx + 6)) / 2;
      int count;
      if (raw) {
        if (idx < 2) return false;
        int pci = (int)strtol(line.substring(idx - 2, idx).c_str(), nullptr, 16);
        count = pci - 3;
        if (count < 1 || count > 4) return false;
        if (count > bytesAfter) count = bytesAfter;
      } else {
        count = bytesAfter > 4 ? 4 : bytesAfter;
      }
      for (int i = 0; i < count; i++) {
        d[i] = (uint8_t)strtol(line.substring(idx + 6 + i * 2, idx + 8 + i * 2).c_str(), nullptr, 16);
      }
      *n = count;
      return count > 0;
    }
    start = end + 1;
  }
  return false;
}

static void bmwRestore() {
  cmd("ATCRA", 2000);
  cmd("ATCEA", 2000);
  cmd("ATCAF1", 2000);
  cmd("ATH0", 2000);
  cmd("ATSH7DF", 2000);
}

static bool bmwQuery(int variant, uint8_t* d, int* n, String* replyOut) {
  String r;
  bool raw = variant < 2;
  if (variant < 2) {
    cmd("ATCAF0", 2000);
    cmd("ATSH6F1", 2000);
    cmd("ATH1", 2000);
    cmd(variant == 0 ? "ATCRA612" : "ATCRA", 2000);
    r = cmd("1203224402", 2000);
  } else {
    cmd("ATCAF1", 2000);
    cmd("ATSH6F1", 2000);
    cmd("ATH0", 2000);
    cmd("ATCEA12", 2000);
    cmd("ATCRA612", 2000);
    r = cmd("224402", 2000);
  }
  if (replyOut) *replyOut = r;
  return bmwParse(r, raw, d, n);
}

static void bmwDecode(const uint8_t* d, int n, float* a, float* b) {
  if (n == 1) {
    *a = (float)d[0] - 60.0f;
    *b = d[0] * 0.75f - 48.0f;
  } else {
    uint16_t raw = ((uint16_t)d[0] << 8) | d[1];
    *a = (float)(int16_t)raw * 0.1f;
    *b = raw * 0.75f - 48.0f;
  }
}

static bool bmwPlausible(float t) { return t > -40.0f && t < 200.0f; }

static bool bmwReadOil(float& out) {
  uint8_t d[4];
  int n = 0;
  String reply;
  bool ok = bmwQuery(bmwVariant, d, &n, &reply);
  bmwRestore();
  if (!ok) return false;
  float a, b;
  bmwDecode(d, n, &a, &b);
  out = bmwFormula == 0 ? a : b;
  return bmwPlausible(out);
}

static void probeBmw() {
  Log.println("\n--- BMW engine ECU request (22 4402 to the DME at 0x12) ---");
  bmwOil = false;
  if (worked[0x5C]) {
    Log.println("  Standard PID 5C already works, so no BMW request is needed");
    return;
  }
  const char* names[] = {"raw frame, reply filter 612", "raw frame, no reply filter", "extended addressing 12"};
  for (int v = 0; v < 3; v++) {
    uint8_t d[4];
    int n = 0;
    String reply;
    bool ok = bmwQuery(v, d, &n, &reply);
    Log.printf("  variant %d (%s): %s\n", v, names[v], reply.c_str());
    if (!ok) continue;
    float a, b;
    bmwDecode(d, n, &a, &b);
    float coolant = NAN;
    readValue(0x05, coolant);
    Log.printf("  got %d byte(s), reading A %.1f C, reading B %.1f C, coolant %.1f C\n", n, a, b, coolant);
    bool pa = bmwPlausible(a), pb = bmwPlausible(b);
    if (pa && pb && !isnan(coolant)) bmwFormula = fabsf(a - coolant) <= fabsf(b - coolant) ? 0 : 1;
    else bmwFormula = pa ? 0 : 1;
    bmwVariant = v;
    bmwOil = true;
    put(g_live.oilC, bmwFormula == 0 ? a : b);
    Log.printf("  using variant %d and reading %c\n", v, bmwFormula == 0 ? 'A' : 'B');
    break;
  }
  if (!bmwOil) Log.println("  No usable reply from the DME, oil temperature stays unavailable");
  bmwRestore();
}

static void printSummary() {
  Log.println("\n--- Summary: will each planned screen work? ---");
  summaryLine("RPM", worked[0x0C], "PID 0C");
  summaryLine("Speed", worked[0x0D], "PID 0D");
  summaryLine("Coolant", worked[0x05], "PID 05");
  summaryLine("Oil temp", worked[0x5C] || renaultOil || bmwOil,
              worked[0x5C] ? "PID 5C" : (renaultOil ? "Renault request 22 2007" : (bmwOil ? "BMW request 22 4402" : "not available")));
  summaryLine("Boost", worked[0x0B] || renaultBoost,
              renaultBoost ? "Renault boost pressure 22 2401"
                           : (worked[0x33] ? "MAP 0B minus baro 33"
                                           : (worked[0x0B] ? "MAP 0B, baro assumed 101.3 kPa" : "MAP 0B not available")));
  summaryLine("Voltage", worked[0x42] || renaultVolt,
              worked[0x42] ? "PID 42" : (renaultVolt ? "Renault request 22 2005" : "PID 42 not available (ATRV adapter voltage still works)"));
  bool lambdaOk = worked[0x24] || worked[0x25] || worked[0x34] || worked[0x35];
  bool o2Volts = worked[0x14] || worked[0x15];
  summaryLine("Lambda", lambdaOk,
              lambdaOk ? "lambda PIDs" : (o2Volts ? "only O2 voltage PIDs" : "no O2 PIDs"));
  Log.println("Check the values above against the car: coolant vs the dash gauge, speed vs the speedo,");
  Log.println("RPM at idle, and MAP/boost near 0 bar at idle.\n");
}

static void probe() {
  memset(supported, 0, sizeof supported);
  memset(worked, 0, sizeof worked);
  Log.println("\n--- Probing supported PIDs ---");

  Log.printf("0100 warm-up -> %s\n", cmd("0100", 10000).c_str());
  Log.printf("ATDP -> %s\n", cmd("ATDP", 3000).c_str());

  uint8_t d[8];
  for (int base = 0x00; base <= 0x60; base += 0x20) {
    int n = readPid(base, d, 4);
    if (n < 4) {
      Log.printf("PID %02X: no valid reply (ignition on? engine running?)\n", base);
      break;
    }
    for (int i = 0; i < 32; i++) {
      if (d[i / 8] & (0x80 >> (i % 8))) supported[base + 1 + i] = true;
    }
    if (!(d[3] & 0x01)) break;
  }

  Log.print("Supported PIDs:");
  for (int p = 1; p <= 0x80; p++) {
    if (supported[p]) Log.printf(" %02X", p);
  }
  Log.println();

  Log.println("\nKey readings:");
  for (const KeyPid& k : KEYS) {
    if (!supported[k.pid]) {
      Log.printf("  %-18s (01%02X): NOT supported\n", k.name, k.pid);
      continue;
    }
    float v;
    if (readValue(k.pid, v)) {
      worked[k.pid] = true;
      Log.printf("  %-18s (01%02X): supported, %.*f %s\n", k.name, k.pid, decimals(k), v, k.unit);
    } else {
      Log.printf("  %-18s (01%02X): supported, but no usable reply\n", k.name, k.pid);
    }
  }
  if (g_settings.vehicle == "meganers3" || g_settings.vehicle == "clio4rs" || g_settings.vehicle == "megane4rs") probeRenault();
  if (g_settings.vehicle == "bmwe8x" || g_settings.vehicle == "bmwe8xturbo") probeBmw();
  printSummary();
  if (supported[0x0D]) speedRate(3000);
  Log.println("--- Probe done ---\n");
}

static void pollOnce() {
  uint32_t t0 = millis();
  String line;
  float map = NAN, baro = NAN;

  for (const KeyPid& k : KEYS) {
    if (!supported[k.pid]) continue;
    float v;
    if (!readValue(k.pid, v)) continue;
    switch (k.pid) {
      case 0x0C: put(g_live.rpm, v); break;
      case 0x0D: put(g_live.speedKph, v); break;
      case 0x05: put(g_live.coolantC, v); break;
      case 0x5C: if (!renaultOil) put(g_live.oilC, v); break;
      case 0x0B: if (!renaultBoost) put(g_live.mapKpa, v); break;
      case 0x33: if (!renaultBoost) g_live.baroKpa = v; break;
      default: break;
    }
    if (k.pid == 0x0B) map = v;
    if (k.pid == 0x33) baro = v;
    char buf[48];
    snprintf(buf, sizeof buf, "%s %.*f%s | ", k.name, decimals(k), v, k.unit);
    line += buf;
  }

  static uint32_t cycle = 0;
  bool slow = (cycle++ % 10) == 0;
  float rv;
  if (renaultBoost && readDidValue(0x2401, rv)) {
    float baroMbar = isnan(renaultBaroMbar) ? 1013.0f : renaultBaroMbar;
    float absMbar = boostAbsolute ? rv : rv + baroMbar;
    map = absMbar / 10.0f;
    baro = baroMbar / 10.0f;
    put(g_live.mapKpa, map);
    g_live.baroKpa = baro;
  }
  if (slow) {
    if (renaultBaro && readDidValue(0x2009, rv)) renaultBaroMbar = rv;
    if (renaultOil && readDidValue(0x2007, rv)) put(g_live.oilC, rv);
    if (renaultVolt && readDidValue(0x2005, rv)) put(g_live.batteryV, rv);
    if (!renaultVolt) {
      float av = cmd("ATRV", 2000).toFloat();
      if (av > 0.0f) put(g_live.batteryV, av);
    }
  }
  if (renaultOil && !isnan(g_live.oilC)) {
    char buf[32];
    snprintf(buf, sizeof buf, "Oil %.1fC | ", (float)g_live.oilC);
    line += buf;
  }
  if (!isnan(g_live.batteryV)) {
    char buf[32];
    snprintf(buf, sizeof buf, "Batt %.2fV | ", (float)g_live.batteryV);
    line += buf;
  }

  if (!isnan(map)) {
    float b = isnan(baro) ? 101.3f : baro;
    char buf[48];
    snprintf(buf, sizeof buf, "BOOST %.2f bar | ", (map - b) / 100.0f);
    line += buf;
  }

  Log.printf("%s%ums\n", line.c_str(), (unsigned)(millis() - t0));
}

static String lineBuf;

static bool pollAllMode = false;

static float thrZero = NAN;
static float thrSaved = NAN;
static float thrRaw = NAN;
static bool thrLoaded = false;

static float throttleScaled(float raw) {
  if (!thrLoaded) {
    thrLoaded = true;
    Preferences p;
    if (p.begin("ventpuck", true)) {
      thrZero = p.getFloat("thrZero", NAN);
      p.end();
    }
    thrSaved = thrZero;
  }
  thrRaw = raw;
  static int lowRun = 0;
  static float lowMax = 0.0f;
  if (raw < 30.0f && (isnan(thrZero) || raw < thrZero)) {
    lowMax = lowRun == 0 ? raw : fmaxf(lowMax, raw);
    if (++lowRun >= 5) {
      thrZero = lowMax;
      lowRun = 0;
    }
  } else {
    lowRun = 0;
  }
  if (!isnan(thrZero) && (isnan(thrSaved) || thrSaved - thrZero >= 0.5f)) {
    Preferences p;
    if (p.begin("ventpuck", false)) {
      p.putFloat("thrZero", thrZero);
      p.end();
      thrSaved = thrZero;
    }
  }
  float z = isnan(thrZero) ? 0.0f : thrZero;
  float out = (raw - z) * 100.0f / (100.0f - z);
  if (out < 0.0f) out = 0.0f;
  if (out > 100.0f) out = 100.0f;
  return out;
}

static const uint32_t TEMP_MS = 2000;
static const uint32_t VOLT_MS = 1000;

static void pollNeeded() {
  static uint32_t cycle = 0;
  static uint32_t lastLog = 0;
  uint32_t t0 = millis();
  bool slow = (cycle++ % 10) == 0;
  uint32_t mainMask = g_want.main;
  uint32_t secMask = g_want.secondary;
  bool any = false;
  float v;
  auto need = [&](int bit) { return (mainMask & (1u << bit)) || (slow && (secMask & (1u << bit))); };
  static uint32_t lastSlowRead[10] = {0};
  uint32_t now = millis();
  auto needSlow = [&](int bit, float current, uint32_t everyMs) {
    if (!((mainMask | secMask) & (1u << bit))) return false;
    uint32_t wait = isnan(current) ? 500 : everyMs;
    if (lastSlowRead[bit] && now - lastSlowRead[bit] < wait) return false;
    lastSlowRead[bit] = now;
    return true;
  };

  if (need(M_RPM) && readValue(0x0C, v)) {
    put(g_live.rpm, v);
    any = true;
  }
  if (need(M_SPEED) && readValue(0x0D, v)) {
    put(g_live.speedKph, v);
    g_live.speedStamp = millis();
    any = true;
  }
  if (needSlow(M_COOLANT, g_live.coolantC, TEMP_MS) && readValue(0x05, v)) {
    put(g_live.coolantC, v);
    any = true;
  }
  if (needSlow(M_OIL, g_live.oilC, TEMP_MS)) {
    if (renaultOil) {
      if (readDidValue(0x2007, v)) {
        put(g_live.oilC, v);
        any = true;
      }
    } else if (bmwOil) {
      if (bmwReadOil(v)) {
        put(g_live.oilC, v);
        any = true;
      }
    } else if (worked[0x5C] && readValue(0x5C, v)) {
      put(g_live.oilC, v);
      any = true;
    }
  }
  if (need(M_BOOST)) {
    if (renaultBoost) {
      if (slow && renaultBaro && readDidValue(0x2009, v)) renaultBaroMbar = v;
      if (readDidValue(0x2401, v)) {
        float baroMbar = isnan(renaultBaroMbar) ? 1013.0f : renaultBaroMbar;
        float absMbar = boostAbsolute ? v : v + baroMbar;
        put(g_live.mapKpa, absMbar / 10.0f);
        g_live.baroKpa = baroMbar / 10.0f;
        any = true;
      }
    } else {
      if (slow && worked[0x33] && readValue(0x33, v)) g_live.baroKpa = v;
      if (readValue(0x0B, v)) {
        put(g_live.mapKpa, v);
        any = true;
      }
    }
  }
  if (needSlow(M_VOLTAGE, g_live.batteryV, VOLT_MS)) {
    if (renaultVolt) {
      if (readDidValue(0x2005, v)) {
        put(g_live.batteryV, v);
        any = true;
      }
    } else {
      float av = cmd("ATRV", 2000).toFloat();
      if (av > 0.0f) {
        put(g_live.batteryV, av);
        any = true;
      }
    }
  }
  if (worked[0x0F] && needSlow(M_IAT, g_live.iatC, TEMP_MS) && readValue(0x0F, v)) {
    put(g_live.iatC, v);
    any = true;
  }
  if (worked[0x04] && need(M_LOAD) && readValue(0x04, v)) {
    put(g_live.loadPct, v);
    any = true;
  }
  if (worked[0x11] && need(M_THROTTLE) && readValue(0x11, v)) {
    put(g_live.throttlePct, throttleScaled(v));
    any = true;
  }
  if (worked[0x46] && needSlow(WANT_AMBIENT, g_live.ambientC, TEMP_MS) && readValue(0x46, v)) {
    put(g_live.ambientC, v);
    any = true;
  }
  if (!any) delay(30);
  if (millis() - lastLog > 1000) {
    lastLog = millis();
    Log.printf("rpm %.0f speed %.0f coolant %.1f oil %.1f map %.1f baro %.1f batt %.2f load %.1f thr %.1f (raw %.1f) | want %02X/%02X | %ums\n",
                  (float)g_live.rpm, (float)g_live.speedKph, (float)g_live.coolantC, (float)g_live.oilC,
                  (float)g_live.mapKpa, (float)g_live.baroKpa, (float)g_live.batteryV, (float)g_live.loadPct,
                  (float)g_live.throttlePct, thrRaw, (unsigned)mainMask, (unsigned)secMask, (unsigned)(millis() - t0));
  }
}

static bool replyHasFailure(const String& r) {
  return r.indexOf("NO DATA") >= 0 || r.indexOf("UNABLE") >= 0 || r.indexOf("TIMEOUT") >= 0 ||
         r.indexOf("ERROR") >= 0 || r.indexOf('?') >= 0 || r.indexOf("WRITE FAILED") >= 0;
}

static void handleDtcCommand() {
  DtcCommand c = g_dtc.command;
  if (c == DTC_NONE) return;
  g_dtc.command = DTC_NONE;
  if (c == DTC_SCAN) {
    g_dtc.state = DTCS_SCANNING;
    String r = cmd("03", 6000);
    DtcList list;
    dtcParse(r.c_str(), &list);
    Log.printf("DTC scan reply: %s | %d code(s)\n", r.c_str(), list.count);
    if (list.count == 0 && replyHasFailure(r)) {
      g_dtc.count = 0;
      g_dtc.state = DTCS_ERROR;
      return;
    }
    for (int i = 0; i < list.count; i++) strcpy(g_dtc.codes[i], list.codes[i]);
    g_dtc.count = list.count;
    g_dtc.state = DTCS_DONE;
  } else if (c == DTC_CLEAR) {
    g_dtc.state = DTCS_SCANNING;
    String r = cmd("04", 6000);
    Log.printf("DTC clear reply: %s\n", r.c_str());
    if (r.indexOf("44") >= 0 && !replyHasFailure(r)) {
      g_dtc.count = 0;
      g_dtc.state = DTCS_CLEARED;
    } else {
      g_dtc.state = DTCS_ERROR;
    }
  }
}

static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c != '\n' && c != '\r') {
      lineBuf += c;
      continue;
    }
    lineBuf.trim();
    if (lineBuf.length()) {
      if (lineBuf.equalsIgnoreCase("poll off")) {
        pollEnabled = false;
        Log.println("Polling off");
      } else if (lineBuf.equalsIgnoreCase("poll on")) {
        pollEnabled = true;
        Log.println("Polling on");
      } else if (lineBuf.equalsIgnoreCase("poll all")) {
        pollAllMode = true;
        Log.println("Polling everything");
      } else if (lineBuf.equalsIgnoreCase("poll ui")) {
        pollAllMode = false;
        Log.println("Polling what the screen needs");
      } else if (lineBuf.equalsIgnoreCase("probe")) {
        probe();
      } else if (lineBuf.equalsIgnoreCase("bmw")) {
        probeBmw();
      } else if (lineBuf.equalsIgnoreCase("reinit")) {
        initAdapter();
      } else {
        Log.printf("> %s\n< %s\n", lineBuf.c_str(), cmd(lineBuf).c_str());
      }
    }
    lineBuf = "";
  }
}

LiveData g_live;
WantData g_want;
DtcStatus g_dtc;

static volatile bool stopping = false;
static volatile bool stopped = false;

static void parkTask() {
  esp_task_wdt_delete(nullptr);
  if (client && client->isConnected()) {
    client->disconnect();
    uint32_t t0 = millis();
    while (client->isConnected() && millis() - t0 < 1000) delay(10);
  }
  stopped = true;
  for (;;) vTaskSuspend(nullptr);
}

static void watchdogStart() {
  esp_task_wdt_config_t cfg = {.timeout_ms = WDT_MS, .idle_core_mask = 1 << 0, .trigger_panic = true};
  esp_err_t e = esp_task_wdt_reconfigure(&cfg);
  if (e == ESP_ERR_INVALID_STATE) e = esp_task_wdt_init(&cfg);
  if (e == ESP_OK) e = esp_task_wdt_add(nullptr);
  Log.printf("OBD watchdog: %s\n", e == ESP_OK ? "on" : esp_err_to_name(e));
}

static void obdTask(void*) {
  Log.println("\nVentPuck OBD starting");
  watchdogStart();
  NimBLEDevice::init("VentPuck");
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(true, false, true);
  const uint8_t keyDistEnc = 0x01;
  NimBLEDevice::setSecurityInitKey(keyDistEnc);
  NimBLEDevice::setSecurityRespKey(keyDistEnc);
  Log.printf("Stored bonds: %d\n", NimBLEDevice::getNumBonds());

  for (;;) {
    g_live.state = OBD_SEARCHING;
    esp_task_wdt_reset();
    while (!connectAdapter()) {
      esp_task_wdt_reset();
      if (stopping) parkTask();
      Log.println("Retrying in 3 s...\n");
      delay(3000);
    }
    esp_task_wdt_reset();
    if (stopping) parkTask();
    g_live.state = OBD_CONNECTED;
    lastTimedOut = false;
    initAdapter();
    probe();
    g_live.state = OBD_LIVE;

    while (!stopping && client && client->isConnected()) {
      esp_task_wdt_reset();
      handleSerial();
      handleDtcCommand();
      pauseActive = pauseRequested;
      if (pollEnabled && !pauseActive) {
        if (pollAllMode) pollOnce();
        else pollNeeded();
      } else {
        delay(50);
      }
      expireStale();
    }
    if (stopping) parkTask();
    Log.println("Disconnected, reconnecting...");
    clearLive();
    delay(1000);
  }
}

void obdStop() { stopping = true; }

void obdPause(bool pause) {
  pauseRequested = pause;
  if (!pause) {
    pauseActive = false;
    return;
  }
  uint32_t t0 = millis();
  while (g_live.state == OBD_LIVE && !pauseActive && millis() - t0 < 3000) delay(20);
}

bool obdStopped() { return stopped || g_live.state == OBD_STARTING || g_live.state == OBD_SEARCHING; }

void obdStart() {
  xTaskCreatePinnedToCore(obdTask, "obd", 12288, nullptr, 1, nullptr, 0);
}
