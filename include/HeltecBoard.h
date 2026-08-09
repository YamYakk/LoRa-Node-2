#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <SSD1306Wire.h>
#include "esp_sleep.h"

namespace Heltec {

  // --- Board pins (Heltec WiFi LoRa 32 V3) ---
  static constexpr int PIN_VEXT      = 36; // LOW=ON, HIGH=OFF
  static constexpr int PIN_OLED_RST  = 21;
  static constexpr int PIN_SDA       = 17;
  static constexpr int PIN_SCL       = 18;
  static constexpr int PIN_BATT_ADC  = 1;  // ADC1_CH0
  static constexpr int PIN_BATT_EN   = 37; // HIGH=divider ON

  static constexpr float VBAT_DIVIDER = 4.95f; // tweak to match DMM

  // Internal SSD1306 instance
  static SSD1306Wire display(0x3C, PIN_SDA, PIN_SCL);

  inline void vextOn()        { pinMode(PIN_VEXT, OUTPUT); digitalWrite(PIN_VEXT, LOW);  delay(10); }
  inline void vextOff()       { pinMode(PIN_VEXT, OUTPUT); digitalWrite(PIN_VEXT, HIGH); }
  inline void oledResetHigh() { pinMode(PIN_OLED_RST, OUTPUT); digitalWrite(PIN_OLED_RST, HIGH); delay(5); }
  inline void oledResetLow()  { pinMode(PIN_OLED_RST, OUTPUT); digitalWrite(PIN_OLED_RST, LOW);  }
  inline void battSenseOn()   { pinMode(PIN_BATT_EN, OUTPUT); digitalWrite(PIN_BATT_EN, HIGH); }
  inline void battSenseOff()  { pinMode(PIN_BATT_EN, OUTPUT); digitalWrite(PIN_BATT_EN, LOW);  }

  inline void begin() {
    vextOn();
    oledResetHigh();
    Wire.begin(PIN_SDA, PIN_SCL);
    display.init();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    battSenseOn();
    analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
  }

  inline uint16_t readBatteryMilliVolts() {
    delayMicroseconds(200);
    uint32_t acc = 0;
    for (int i = 0; i < 8; ++i) {
      acc += analogReadMilliVolts(PIN_BATT_ADC);
      delayMicroseconds(150);
    }
    float mv_pin = acc / 8.0f;
    float mv_batt = mv_pin * VBAT_DIVIDER;
    if (mv_batt < 2000) mv_batt = 2000;
    if (mv_batt > 5000) mv_batt = 5000;
    return (uint16_t)mv_batt;
  }

  inline void render(uint32_t hh, uint32_t mm, uint32_t ss,
                     const String &battLine, uint32_t cycleCount, uint32_t secondsLeft) {
    char tbuf[9];
    snprintf(tbuf, sizeof(tbuf), "%02u:%02u:%02u", (unsigned)hh, (unsigned)mm, (unsigned)ss);
    char cbuf[24];
    snprintf(cbuf, sizeof(cbuf), "Cycle %lu  T-%lus",
             (unsigned long)cycleCount, (unsigned long)secondsLeft);

    display.clear();
    display.setFont(ArialMT_Plain_24); display.drawString(64, 12, tbuf);
    display.setFont(ArialMT_Plain_16); display.drawString(64, 36, battLine);
    display.setFont(ArialMT_Plain_10); display.drawString(64, 52, cbuf);
    display.display();
  }

  [[noreturn]] inline void deepSleep(uint32_t seconds) {
    oledResetLow();
    vextOff();
    battSenseOff();
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_deep_sleep_start();
    while (true) {}
  }

} // namespace Heltec
