// Copyright © Charlie Howard 2026 All rights reserved
#include "rollback.h"
#include <Arduino.h>
#include <Preferences.h>
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "sdlog.h"
#include "version.h"
#include "versionCompare.h"

static const char* NS = "ventpuck";
static const uint32_t TRIAL_MS = 5UL * 60UL * 1000UL;
static const uint8_t MAX_CRASHES = 3;
static bool onTrial = false;

static bool sameVersion(const char* a, const char* b) {
  FwVersion va, vb;
  return versionParse(a, &va) && versionParse(b, &vb) && versionCompare(va, vb) == 0;
}

static bool isCrash(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

static void endTrial(Preferences& p) {
  p.remove("trialVer");
  p.remove("trialCrash");
}

static void rollBack(Preferences& p) {
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  esp_app_desc_t desc;
  if (!other || esp_ota_get_partition_description(other, &desc) != ESP_OK) {
    Log.println("Rollback: no previous version to go back to");
    endTrial(p);
    return;
  }
  esp_err_t e = esp_ota_set_boot_partition(other);
  if (e != ESP_OK) {
    Log.printf("Rollback: couldn't switch to %s (%s)\n", other->label, esp_err_to_name(e));
    endTrial(p);
    return;
  }
  p.putString("badVer", FW_VERSION);
  endTrial(p);
  p.end();
  Log.printf("Rollback: %s kept crashing, going back to the previous version in %s\n", FW_VERSION, other->label);
  delay(300);
  esp_restart();
}

void rollbackBoot() {
  Preferences p;
  if (!p.begin(NS, false)) return;
  String trial = p.getString("trialVer", "");
  if (!trial.length() || !sameVersion(trial.c_str(), FW_VERSION)) {
    if (trial.length()) endTrial(p);
    p.end();
    return;
  }
  esp_reset_reason_t reason = esp_reset_reason();
  uint8_t crashes = p.getUChar("trialCrash", 0);
  if (isCrash(reason)) {
    crashes++;
    p.putUChar("trialCrash", crashes);
  }
  Log.printf("Rollback: %s on trial, %u crash restart(s) so far (reset reason %d)\n", FW_VERSION, crashes,
             (int)reason);
  if (crashes >= MAX_CRASHES) {
    rollBack(p);
    p.end();
    return;
  }
  onTrial = true;
  p.end();
}

void rollbackLoop() {
  if (!onTrial || millis() < TRIAL_MS) return;
  onTrial = false;
  Preferences p;
  if (!p.begin(NS, false)) return;
  endTrial(p);
  p.end();
  Log.printf("Rollback: %s ran for 5 minutes, keeping it\n", FW_VERSION);
}

void rollbackArm(const char* tag) {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putString("trialVer", tag);
  p.putUChar("trialCrash", 0);
  p.end();
}

bool rollbackIsBad(const char* tag) {
  Preferences p;
  if (!p.begin(NS, true)) return false;
  String bad = p.getString("badVer", "");
  p.end();
  return bad.length() && sameVersion(bad.c_str(), tag);
}
