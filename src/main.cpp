// ============================================================================
// main.cpp — RadioLib LoRaWAN OTAA with session persistence + Heltec V3 Battery
// Target: ESP32/ESP32-S3 + RadioLib (LoRaWAN)
// Requires:
//   - jgromes/RadioLib with LoRaWAN support
//   - Arduino-ESP32 core (Preferences + analogReadMilliVolts)
//   - Your "config_node5.h" must define:
//       radio, node, joinEUI, devEUI, nwkKey, appKey, uplinkIntervalSeconds
//
// What this does
//   1) First run: OTAA join, then after first successful uplink it saves the
//      session (keys, nonces, FCnt) to NVS.
//   2) Reboot: restores session (no join), sends uplinks immediately, and saves
//      the session after every uplink to keep FCnt in sync.
//   3) Reads battery voltage on Heltec WiFi LoRa 32 V3 by pulling GPIO37 LOW to
//      route VBAT to GPIO1 through a 390k/100k divider, then reads ADC.
//
// Tip: If you ever need to force a fresh join, temporarily clear NVS "lw"
//      namespace (see optional factory reset snippet below).
// ============================================================================
// --- add this include at top ---
// REPLACE WHOLE FILE

#include <Arduino.h>
#include <Preferences.h>
#include "config_node5.h"    // radio, node, joinEUI/devEUI/keys, Region/subBand
#include "HeltecBoard.h"     // your board helpers (VEXT/OLED/batt/deepSleep)
#include "Battery.h"
#include "ChipTemp.h"

RTC_DATA_ATTR uint32_t cycleCount = 0;

// ---------- LoRaWAN session persistence ----------
Preferences _prefs;  // NVS namespace "lw"

// Try to restore saved session (nonces + session buffers).
// Returns true if RadioLib reports SESSION_RESTORED.
static bool lwRestore(LoRaWANNode& n) {
  if (!_prefs.begin("lw", false)) return false;

  if (_prefs.getBytesLength("n") != RADIOLIB_LORAWAN_NONCES_BUF_SIZE ||
      _prefs.getBytesLength("s") != RADIOLIB_LORAWAN_SESSION_BUF_SIZE) {
    _prefs.end();
    return false;
  }

  uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  uint8_t sess  [RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
  _prefs.getBytes("n", nonces, sizeof(nonces));
  _prefs.getBytes("s", sess,   sizeof(sess));
  _prefs.end();

  if (n.setBufferNonces(nonces)  != RADIOLIB_ERR_NONE) return false;
  if (n.setBufferSession(sess)   != RADIOLIB_ERR_NONE) return false;

  int16_t st = n.activateOTAA();  // returns SESSION_RESTORED if accepted
  return (st == RADIOLIB_LORAWAN_SESSION_RESTORED);
}

// Save current session + nonces after a successful uplink to keep FCnt aligned.
static void lwSave(LoRaWANNode& n) {
  uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  uint8_t sess  [RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
  memcpy(nonces, n.getBufferNonces(),  sizeof(nonces));
  memcpy(sess,   n.getBufferSession(), sizeof(sess));
  if (_prefs.begin("lw", false)) {
    _prefs.putBytes("n", nonces, sizeof(nonces));
    _prefs.putBytes("s", sess,   sizeof(sess));
    _prefs.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(30);

  // === ONE-TIME NVS RESET (clear saved LoRaWAN session) ===
  // Flash once with these 3 lines uncommented, power-cycle, then comment/remove.
  // Preferences p; p.begin("lw", false); p.clear(); p.end();
  // === END ONE-TIME RESET ===

  cycleCount++;
  Heltec::begin();  // power VEXT, init OLED + batt ADC

  // --- (optional) quick wake banner — app logic lives in main, not header ---
  {
    String banner = String("WAKE ") + String(cycleCount);
    Heltec::display.clear();
    Heltec::display.setFont(ArialMT_Plain_16);
    Heltec::display.setTextAlignment(TEXT_ALIGN_CENTER);
    Heltec::display.drawString(64, 24, banner);
    Heltec::display.display();
    delay(2000);
  }

  // -------- Radio + LoRaWAN --------
  int16_t st = radio.begin();
  if (st != RADIOLIB_ERR_NONE) goto SLEEP;

  st = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  if (st != RADIOLIB_ERR_NONE) goto SLEEP;

  // Restore previous session if available; else join once
  if (!lwRestore(node)) {
    st = node.activateOTAA();
    if (st != RADIOLIB_LORAWAN_NEW_SESSION) goto SLEEP;
  }

  // -------- ONE uplink per wake --------
  {
    uint16_t batt_mv = Heltec::readBatteryMilliVolts();
    uint8_t  v1 = radio.random(100);
    uint16_t v2 = radio.random(2000);

    uint8_t payload[5] = {
      v1,
      (uint8_t)(v2 >> 8), (uint8_t)v2,
      (uint8_t)(batt_mv >> 8), (uint8_t)batt_mv
    };

    st = node.sendReceive(payload, sizeof(payload));
    if (st >= RADIOLIB_ERR_NONE) {
      lwSave(node);                // CRITICAL: save after successful uplink
    }
  }

SLEEP:
  // quick status (optional)
  Heltec::display.clear();
  Heltec::display.setTextAlignment(TEXT_ALIGN_CENTER);
  Heltec::display.setFont(ArialMT_Plain_16);
  Heltec::display.drawString(64, 24, (st >= 0) ? "Sent ✓" : "Send fail");
  Heltec::display.display();
  delay(2000);

  // sleep ~20s; all rails are powered down inside your header
  Heltec::deepSleep(300);
}

void loop() {
  // never reached
}

