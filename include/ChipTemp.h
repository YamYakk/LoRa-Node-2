#pragma once
#include <Arduino.h>

// READ ME:
// - On ESP32‑S3, Arduino's temperatureRead() returns °C (rough, needs offset).
// - Expect poor absolute accuracy (±5–10 °C). Calibrate with TEMP_OFFSET_C.

#ifndef TEMP_OFFSET_C
#define TEMP_OFFSET_C 0.0f   // tweak after comparing to a known-good thermometer
#endif

inline float chipTempC_raw() {
  // Works on ESP32‑S3 in Arduino-ESP32 core 2.x+
  return temperatureRead();
}

inline float chipTempC() {
  return chipTempC_raw() + TEMP_OFFSET_C;
}

// Simple 1‑pole IIR smoothing (0..1). Keep state in the caller.
inline float smoothTemp(float prev, float now, float alpha = 0.2f) {
  return (1.0f - alpha) * prev + alpha * now;
}

// Format helper: "28.6°C"
inline String chipTempStr(float celsius) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f°C", celsius);
  return String(buf);
}
