// ============================================================================
// Heltec WiFi LoRa 32 V3  — BMP180 Read (uses SDA=41, SCL=42)
// Minimal sketch: no LoRa, no OLED. Just temperature & pressure.
// Wiring: VCC->3V3, GND->GND, SDA->41, SCL->42
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>

static const int I2C_SDA = 41;      // found by your scan
static const int I2C_SCL = 42;
TwoWire I2Cext = TwoWire(1);        // use controller #1 (avoid OLED bus)
Adafruit_BMP085 bmp;                // Adafruit BMP085/BMP180 driver
bool bmp_ok = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  delay(200);

  Serial.println("\n=== BMP180 Read (Heltec V3) ===");
  Serial.printf("I2C on SDA=%d SCL=%d @100kHz\n", I2C_SDA, I2C_SCL);

  // Bring up I²C on header pins
  I2Cext.begin(I2C_SDA, I2C_SCL, 100000);

  // Init BMP180 (0x77) on our bus
  bmp_ok = bmp.begin(BMP085_ULTRAHIGHRES, &I2Cext);
  Serial.printf("[BMP180] init: %s\n", bmp_ok ? "OK" : "NOT FOUND (check wiring)");

  if (!bmp_ok) {
    Serial.println("Hint: confirm power on 3V3 and pins 41/42.");
  }
}

void loop() {
  if (bmp_ok) {
    float tC   = bmp.readTemperature();        // °C
    float tF   = tC * 9.0f / 5.0f + 32.0f;     // °F
    int32_t pPa = bmp.readPressure();          // Pa
    float p_hPa = pPa / 100.0f;                // hPa = mbar
    // Simple altitude estimate (set to your local sea-level pressure if known)
    float alt_m = bmp.readAltitude(101325.0f); // using 1013.25 hPa

    Serial.printf("T: %.2f C (%.2f F)  P: %.2f hPa  Alt: %.1f m\n",
                  tC, tF, p_hPa, alt_m);
  } else {
    // don't spam if sensor not found
    static uint32_t last = 0;
    if (millis() - last > 2000) {
      Serial.println("(sensor not initialized)");
      last = millis();
    }
  }
  delay(1000);
}
