// Copyright © Charlie Howard 2026 All rights reserved

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "obd.h"
#include "settings.h"
#include "dtc.h"

static const char* ADAPTER_NAME = "OBDLink";  // matches "OBDLink CX"

static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* txChar = nullptr;  // FFF2 (write)
static NimBLERemoteCharacteristic* rxChar = nullptr;  // FFF1 (notify)

static String rxBuf;
static volatile bool gotPrompt = false;
static bool pollEnabled = true;
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
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '>') {
      gotPrompt = true;
    } else {
      rxBuf += c;
    }
  }
}

static bool uuidHas(const NimBLEUUID& u, const char* fragment) {
  String s = u.toString().c_str();
  s.toLowerCase();
  return s.indexOf(fragment) >= 0;
}

static bool connectAdapter() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  Serial.println("Scanning for OBDLink adapter (8 s)...");
  NimBLEScanResults results = scan->getResults(8000, false);

  const NimBLEAdvertisedDevice* target = nullptr;
  for (uint32_t i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    String name = d->getName().c_str();
    Serial.printf("  found: %s [%s] type %u RSSI %d\n", name.length() ? name.c_str() : "(no name)",
                  d->getAddress().toString().c_str(), d->getAddress().getType(), d->getRSSI());
    if (!target && name.indexOf(ADAPTER_NAME) >= 0) target = d;
  }
  if (!target) {
    Serial.println("No OBDLink adapter found. Is it plugged in, with the ignition on?");
    return false;
  }

  String targetAddr = target->getAddress().toString().c_str();
  if (targetAddr == "00:00:00:00:00:00") {
    Serial.printf("Adapter address resolves to zero (stored bonds: %d), clearing bonds\n", NimBLEDevice::getNumBonds());
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
    Serial.println("Couldn't create a BLE client.");
    return false;
  }
  Serial.printf("Connecting to %s (type %u)...\n", targetAddr.c_str(), target->getAddress().getType());
  if (!client->connect(target)) {
    Serial.println("Connection failed.");
    return false;
  }
  Serial.printf("Connected, MTU %u\n", client->getMTU());

  Serial.printf("Secure connection: %s\n", client->secureConnection() ? "ok" : "not established");

  Serial.println("Services:");
  for (auto* svc : client->getServices(true)) {
    Serial.printf("  service %s\n", svc->getUUID().toString().c_str());
    for (auto* chr : svc->getCharacteristics(true)) {
      Serial.printf("    char %s%s%s%s\n", chr->getUUID().toString().c_str(),
                    chr->canNotify() ? " notify" : "", chr->canWrite() ? " write" : "",
                    chr->canWriteNoResponse() ? " writeNR" : "");
      if (uuidHas(svc->getUUID(), "fff0")) {
        if (uuidHas(chr->getUUID(), "fff1")) rxChar = chr;
        if (uuidHas(chr->getUUID(), "fff2")) txChar = chr;
      }
    }
  }

  if (!rxChar || !txChar) {
    Serial.println("Couldn't find the FFF0 serial service (FFF1 notify / FFF2 write).");
    return false;
  }
  if (!rxChar->subscribe(true, onNotify)) {
    Serial.println("Couldn't subscribe to notifications.");
    return false;
  }
  Serial.println("Subscribed, link ready.\n");
  return true;
}

static bool bleWrite(const String& s) {
  size_t mtu = client->getMTU();
  size_t chunk = mtu > 3 ? mtu - 3 : 20;
  for (size_t i = 0; i < s.length(); i += chunk) {
    size_t end = i + chunk;
    if (end > s.length()) end = s.length();
    String part = s.substring(i, end);
    if (!txChar->writeValue((const uint8_t*)part.c_str(), part.length(), true)) return false;
  }
  return true;
}

static String cmd(const String& c, uint32_t timeoutMs = 5000) {
  rxBuf = "";
  gotPrompt = false;
  if (!bleWrite(c + "\r")) return "WRITE FAILED";
  uint32_t start = millis();
  while (!gotPrompt && millis() - start < timeoutMs) delay(5);
  String r = rxBuf;
  r.trim();
  r.replace("\r", "|");
  return gotPrompt ? r : r + " [TIMEOUT]";
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
  Serial.println("--- Initialising adapter ---");
  const char* init[] = {"ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* c : init) {
    String r = cmd(c, c == init[0] ? 6000 : 3000);
    Serial.printf("%s -> %s\n", c, r.c_str());
    if (c == init[0]) delay(300);
  }
  const char* info[] = {"ATI", "STI", "STDI", "ATRV"};
  for (const char* c : info) {
    Serial.printf("%s -> %s\n", c, cmd(c, 3000).c_str());
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
  Serial.printf("Speed-only polling: %d reads in %.1f s = %.1f reads/s\n", n, secs, n / secs);
}

static void summaryLine(const char* screen, bool ok, const char* detail) {
  Serial.printf("  %-10s %s  %s\n", screen, ok ? "OK    " : "FAILED", detail);
}

static bool testDid(uint16_t did, const char* name, const char* unit, float& out) {
  bool ok = readDidValue(did, out);
  if (ok) Serial.printf("  %-24s (22%04X): %.1f %s\n", name, did, out, unit);
  else Serial.printf("  %-24s (22%04X): no reply\n", name, did);
  return ok;
}

static void probeRenault() {
  Serial.println("\n--- Renault engine ECU requests (service 22 to 7E0) ---");
  cmd("ATSH7E0", 3000);
  float v;
  float boostMbar = NAN;
  renaultOil = testDid(0x2007, "Oil temp", "C", v);
  if (renaultOil) g_live.oilC = v;
  renaultBaro = testDid(0x2009, "Atmospheric pressure", "mbar", v);
  if (renaultBaro) renaultBaroMbar = v;
  renaultBoost = testDid(0x2401, "Boost pressure", "mbar", boostMbar);
  if (renaultBoost) {
    boostAbsolute = boostMbar > 600.0f;
    Serial.printf("  Boost pressure looks %s\n", boostAbsolute ? "absolute (about atmospheric at idle)" : "relative (about zero at idle)");
  }
  renaultVolt = testDid(0x2005, "Battery voltage", "V", v);
  if (renaultVolt) g_live.batteryV = v;
  float extra;
  testDid(0x240B, "Intake manifold pressure", "mbar", extra);
  testDid(0x2402, "Boost setpoint", "hPa", extra);
  if (!renaultOil && !renaultBoost && !renaultBaro && !renaultVolt) {
    Serial.println("  No replies, going back to standard OBD addressing");
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
  Serial.println("\n--- BMW engine ECU request (22 4402 to the DME at 0x12) ---");
  bmwOil = false;
  if (worked[0x5C]) {
    Serial.println("  Standard PID 5C already works, so no BMW request is needed");
    return;
  }
  const char* names[] = {"raw frame, reply filter 612", "raw frame, no reply filter", "extended addressing 12"};
  for (int v = 0; v < 3; v++) {
    uint8_t d[4];
    int n = 0;
    String reply;
    bool ok = bmwQuery(v, d, &n, &reply);
    Serial.printf("  variant %d (%s): %s\n", v, names[v], reply.c_str());
    if (!ok) continue;
    float a, b;
    bmwDecode(d, n, &a, &b);
    float coolant = NAN;
    readValue(0x05, coolant);
    Serial.printf("  got %d byte(s), reading A %.1f C, reading B %.1f C, coolant %.1f C\n", n, a, b, coolant);
    bool pa = bmwPlausible(a), pb = bmwPlausible(b);
    if (pa && pb && !isnan(coolant)) bmwFormula = fabsf(a - coolant) <= fabsf(b - coolant) ? 0 : 1;
    else bmwFormula = pa ? 0 : 1;
    bmwVariant = v;
    bmwOil = true;
    g_live.oilC = bmwFormula == 0 ? a : b;
    Serial.printf("  using variant %d and reading %c\n", v, bmwFormula == 0 ? 'A' : 'B');
    break;
  }
  if (!bmwOil) Serial.println("  No usable reply from the DME, oil temperature stays unavailable");
  bmwRestore();
}

static void printSummary() {
  Serial.println("\n--- Summary: will each planned screen work? ---");
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
  Serial.println("Check the values above against the car: coolant vs the dash gauge, speed vs the speedo,");
  Serial.println("RPM at idle, and MAP/boost near 0 bar at idle.\n");
}

static void probe() {
  memset(supported, 0, sizeof supported);
  memset(worked, 0, sizeof worked);
  Serial.println("\n--- Probing supported PIDs ---");

  Serial.printf("0100 warm-up -> %s\n", cmd("0100", 10000).c_str());
  Serial.printf("ATDP -> %s\n", cmd("ATDP", 3000).c_str());

  uint8_t d[8];
  for (int base = 0x00; base <= 0x60; base += 0x20) {
    int n = readPid(base, d, 4);
    if (n < 4) {
      Serial.printf("PID %02X: no valid reply (ignition on? engine running?)\n", base);
      break;
    }
    for (int i = 0; i < 32; i++) {
      if (d[i / 8] & (0x80 >> (i % 8))) supported[base + 1 + i] = true;
    }
    if (!(d[3] & 0x01)) break;  // next range not supported
  }

  Serial.print("Supported PIDs:");
  for (int p = 1; p <= 0x80; p++) {
    if (supported[p]) Serial.printf(" %02X", p);
  }
  Serial.println();

  Serial.println("\nKey readings:");
  for (const KeyPid& k : KEYS) {
    if (!supported[k.pid]) {
      Serial.printf("  %-18s (01%02X): NOT supported\n", k.name, k.pid);
      continue;
    }
    float v;
    if (readValue(k.pid, v)) {
      worked[k.pid] = true;
      Serial.printf("  %-18s (01%02X): supported, %.*f %s\n", k.name, k.pid, decimals(k), v, k.unit);
    } else {
      Serial.printf("  %-18s (01%02X): supported, but no usable reply\n", k.name, k.pid);
    }
  }
  if (g_settings.vehicle == "meganers3") probeRenault();
  if (g_settings.vehicle == "bmwe8x") probeBmw();
  printSummary();
  if (supported[0x0D]) speedRate(3000);
  Serial.println("--- Probe done ---\n");
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
      case 0x0C: g_live.rpm = v; break;
      case 0x0D: g_live.speedKph = v; break;
      case 0x05: g_live.coolantC = v; break;
      case 0x5C: if (!renaultOil) g_live.oilC = v; break;
      case 0x0B: if (!renaultBoost) g_live.mapKpa = v; break;
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
    g_live.mapKpa = map;
    g_live.baroKpa = baro;
  }
  if (slow) {
    if (renaultBaro && readDidValue(0x2009, rv)) renaultBaroMbar = rv;
    if (renaultOil && readDidValue(0x2007, rv)) g_live.oilC = rv;
    if (renaultVolt && readDidValue(0x2005, rv)) g_live.batteryV = rv;
    if (!renaultVolt) {
      float av = cmd("ATRV", 2000).toFloat();
      if (av > 0.0f) g_live.batteryV = av;
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
    float b = isnan(baro) ? 101.3f : baro;  // assume sea level if baro isn't available
    char buf[48];
    snprintf(buf, sizeof buf, "BOOST %.2f bar | ", (map - b) / 100.0f);
    line += buf;
  }

  Serial.printf("%s%ums\n", line.c_str(), (unsigned)(millis() - t0));
}

static String lineBuf;

static bool pollAllMode = false;

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

  if (need(M_RPM) && readValue(0x0C, v)) {
    g_live.rpm = v;
    any = true;
  }
  if (need(M_SPEED) && readValue(0x0D, v)) {
    g_live.speedKph = v;
    g_live.speedStamp = millis();
    any = true;
  }
  if (need(M_COOLANT) && readValue(0x05, v)) {
    g_live.coolantC = v;
    any = true;
  }
  if (need(M_OIL)) {
    if (renaultOil) {
      if (readDidValue(0x2007, v)) {
        g_live.oilC = v;
        any = true;
      }
    } else if (bmwOil) {
      if (bmwReadOil(v)) {
        g_live.oilC = v;
        any = true;
      }
    } else if (worked[0x5C] && readValue(0x5C, v)) {
      g_live.oilC = v;
      any = true;
    }
  }
  if (need(M_BOOST)) {
    if (renaultBoost) {
      if (slow && renaultBaro && readDidValue(0x2009, v)) renaultBaroMbar = v;
      if (readDidValue(0x2401, v)) {
        float baroMbar = isnan(renaultBaroMbar) ? 1013.0f : renaultBaroMbar;
        float absMbar = boostAbsolute ? v : v + baroMbar;
        g_live.mapKpa = absMbar / 10.0f;
        g_live.baroKpa = baroMbar / 10.0f;
        any = true;
      }
    } else {
      if (slow && worked[0x33] && readValue(0x33, v)) g_live.baroKpa = v;
      if (readValue(0x0B, v)) {
        g_live.mapKpa = v;
        any = true;
      }
    }
  }
  if (need(M_VOLTAGE)) {
    if (renaultVolt) {
      if (readDidValue(0x2005, v)) {
        g_live.batteryV = v;
        any = true;
      }
    } else {
      float av = cmd("ATRV", 2000).toFloat();
      if (av > 0.0f) {
        g_live.batteryV = av;
        any = true;
      }
    }
  }
  if (!any) delay(30);
  if (millis() - lastLog > 1000) {
    lastLog = millis();
    Serial.printf("rpm %.0f speed %.0f coolant %.1f oil %.1f map %.1f baro %.1f batt %.2f | want %02X/%02X | %ums\n",
                  (float)g_live.rpm, (float)g_live.speedKph, (float)g_live.coolantC, (float)g_live.oilC,
                  (float)g_live.mapKpa, (float)g_live.baroKpa, (float)g_live.batteryV, (unsigned)mainMask,
                  (unsigned)secMask, (unsigned)(millis() - t0));
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
    Serial.printf("DTC scan reply: %s | %d code(s)\n", r.c_str(), list.count);
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
    Serial.printf("DTC clear reply: %s\n", r.c_str());
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
        Serial.println("Polling off");
      } else if (lineBuf.equalsIgnoreCase("poll on")) {
        pollEnabled = true;
        Serial.println("Polling on");
      } else if (lineBuf.equalsIgnoreCase("poll all")) {
        pollAllMode = true;
        Serial.println("Polling everything");
      } else if (lineBuf.equalsIgnoreCase("poll ui")) {
        pollAllMode = false;
        Serial.println("Polling what the screen needs");
      } else if (lineBuf.equalsIgnoreCase("probe")) {
        probe();
      } else if (lineBuf.equalsIgnoreCase("bmw")) {
        probeBmw();
      } else if (lineBuf.equalsIgnoreCase("reinit")) {
        initAdapter();
      } else {
        Serial.printf("> %s\n< %s\n", lineBuf.c_str(), cmd(lineBuf).c_str());
      }
    }
    lineBuf = "";
  }
}

LiveData g_live;
WantData g_want;
DtcStatus g_dtc;

static void obdTask(void*) {
  Serial.println("\nVentPuck OBDLink CX test");
  NimBLEDevice::init("VentPuck");
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(true, false, true);
  const uint8_t keyDistEnc = 0x01;
  NimBLEDevice::setSecurityInitKey(keyDistEnc);
  NimBLEDevice::setSecurityRespKey(keyDistEnc);
  Serial.printf("Stored bonds: %d\n", NimBLEDevice::getNumBonds());

  g_live.state = OBD_SEARCHING;
  while (!connectAdapter()) {
    Serial.println("Retrying in 3 s...\n");
    delay(3000);
  }
  g_live.state = OBD_CONNECTED;
  initAdapter();
  probe();
  g_live.state = OBD_LIVE;

  for (;;) {
    if (!client || !client->isConnected()) {
      Serial.println("Disconnected, restarting...");
      delay(1000);
      ESP.restart();
    }
    handleSerial();
    handleDtcCommand();
    if (pollEnabled) {
      if (pollAllMode) pollOnce();
      else pollNeeded();
    } else {
      delay(50);
    }
  }
}

void obdStart() {
  xTaskCreatePinnedToCore(obdTask, "obd", 12288, nullptr, 1, nullptr, 0);
}
