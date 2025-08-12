#pragma once
#include <stdint.h>
#include <Arduino.h>

// Basic Li-ion percentage curve: OCV vs % (simplified)
inline uint8_t batteryPercent(uint16_t mv) {
  // clamp
  if (mv >= 4200) return 100;
  if (mv <= 3300) return 0;

  // Simple linear segments: 4.20–3.90V = top, 3.90–3.70V = mid, 3.70–3.30V = low
  if (mv > 3900) {
    return 80 + (mv - 3900) * 20 / 300; // 80–100%
  } else if (mv > 3700) {
    return 40 + (mv - 3700) * 40 / 200; // 40–80%
  } else {
    return (mv - 3300) * 40 / 400;      // 0–40%
  }
}

// Returns a string like "Batt 3.98 V 76%"
inline String batteryTempLine(uint16_t mv, float tempC) {
  char buf[32];
  snprintf(buf, sizeof(buf), "Batt %.2f V %u%%  %0.1f°C",
           mv / 1000.0f, batteryPercent(mv), tempC);
  return String(buf);
}


