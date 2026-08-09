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
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include "config_node5.h"    // radio, node, joinEUI/devEUI/keys, Region/subBand
#include "HeltecBoard.h"     // your board helpers (VEXT/OLED/batt/deepSleep)
#include "Battery.h"
#include "ChipTemp.h"

RTC_DATA_ATTR uint32_t cycleCount = 0;

// ---------- LoRaWAN session persistence ----------
Preferences _prefs;  // NVS namespace "lw"

// ---------- BMP180 on external I2C bus (#1) ----------
static const int I2C_SDA = 41;
static const int I2C_SCL = 42;
TwoWire I2Cext = TwoWire(1);
Adafruit_BMP085 bmp;
bool bmp_ok = false;

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

  // Bring up external I2C and BMP180
  I2Cext.begin(I2C_SDA, I2C_SCL, 100000);
  bmp_ok = bmp.begin(BMP085_ULTRAHIGHRES, &I2Cext);

  // Wake screen: show battery level and cycle count for 5 seconds
  {
    uint16_t batt_mv = Heltec::readBatteryMilliVolts();
    uint8_t batt_pct = batteryPercent(batt_mv);
    char line1[24];
    snprintf(line1, sizeof(line1), "Batt %.2fV %u%%", batt_mv / 1000.0f, batt_pct);
    String line2 = String("Cycle ") + String(cycleCount);

    Heltec::display.clear();
    Heltec::display.setTextAlignment(TEXT_ALIGN_CENTER);
    Heltec::display.setFont(ArialMT_Plain_16); Heltec::display.drawString(64, 16, line1);
    Heltec::display.setFont(ArialMT_Plain_24); Heltec::display.drawString(64, 36, line2);
    Heltec::display.display();
    delay(5000);
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
    // Measurements
    uint16_t batt_mv   = Heltec::readBatteryMilliVolts();
    uint8_t  batt_pct  = batteryPercent(batt_mv);            // 0..100
    // Prefer BMP180 temp if available; fallback to chip temp
    float    temp_c     = bmp_ok ? bmp.readTemperature() : chipTempC();
    float    temp_f_f   = (temp_c * 9.0f) / 5.0f + 32.0f;    // float °F
    int      temp_f_i   = (int)(temp_f_f + 0.5f);            // round to int
    if (temp_f_i < 0) temp_f_i = 0;                          // clamp
    if (temp_f_i > 999) temp_f_i = 999;

    // BMP180-specific readings for payload extension
    int bmp_temp_f_i = 0;            // BMP_Temp (°F, 3 digits)
    int bmp_inHg10_i = 0;            // BMP_Presurre (inHg*10, 3 digits)
    if (bmp_ok) {
      float bmp_tC   = bmp.readTemperature();
      float bmp_tF   = (bmp_tC * 9.0f) / 5.0f + 32.0f;
      bmp_temp_f_i   = (int)(bmp_tF + 0.5f);
      if (bmp_temp_f_i < 0) bmp_temp_f_i = 0;
      if (bmp_temp_f_i > 999) bmp_temp_f_i = 999;

      int32_t pPa    = bmp.readPressure();               // Pa
      float inHg     = pPa / 3386.389f;                  // 1 inHg = 3386.389 Pa
      float inHg10   = inHg * 10.0f;                     // one decimal place
      bmp_inHg10_i   = (int)(inHg10 + 0.5f);             // round
      if (bmp_inHg10_i < 0) bmp_inHg10_i = 0;
      if (bmp_inHg10_i > 999) bmp_inHg10_i = 999;
    }

    // Scale battery voltage to V*100 to fit 3 digits (e.g., 4.20V -> 420)
    uint16_t batt_v100 = (batt_mv + 5) / 10;                 // round mV/10
    if (batt_v100 > 999) batt_v100 = 999;
    // Clamp percent to two digits
    if (batt_pct > 99) batt_pct = 99;

    // Message counter limited to three digits
    uint16_t msg_cnt = (uint16_t)(cycleCount % 1000U);

    // Build 17-byte ASCII payload: BBB PP TTT CCC BMP_Temp BMP_Presurre (zero-padded)
    // BBB=batt V*100, PP=batt %, TTT=temp°F (prefers BMP), CCC=counter,
    // BMP_Temp=temp°F from BMP180, BMP_Presurre=inHg*10 from BMP180
    char payload[17];
    auto write3 = [](char* p, uint16_t v) {
      p[0] = '0' + (char)((v / 100) % 10);
      p[1] = '0' + (char)((v / 10)  % 10);
      p[2] = '0' + (char)(v % 10);
    };
    auto write2 = [](char* p, uint16_t v) {
      p[0] = '0' + (char)((v / 10) % 10);
      p[1] = '0' + (char)(v % 10);
    };
    write3(&payload[0],  batt_v100);           // BBB
    write2(&payload[3],  batt_pct);            // PP
    write3(&payload[5],  (uint16_t)temp_f_i);  // TTT (°F)
    write3(&payload[8],  msg_cnt);             // CCC
    write3(&payload[11], (uint16_t)bmp_temp_f_i); // BMP_Temp (°F)
    write3(&payload[14], (uint16_t)bmp_inHg10_i); // BMP_Presurre (inHg*10)

    st = node.sendReceive((uint8_t*)payload, sizeof(payload));
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

  // sleep per configured interval (default 60s)
  Heltec::deepSleep(uplinkIntervalSeconds);
}

void loop() {
  // never reached
}

