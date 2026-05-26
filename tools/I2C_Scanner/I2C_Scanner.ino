// I2C Scanner — upload to Mega, open Serial Monitor at 115200
// Reports every device found on the I2C bus

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Wire.begin();
  Serial.println(F("\n== I2C Bus Scanner =="));
  
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print(F("  Device found at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      Serial.println();
      found++;
    }
  }
  if (found == 0) Serial.println(F("  No I2C devices found — check wiring"));
  else { Serial.print(F("  Total devices: ")); Serial.println(found); }
  Serial.println(F("== Scan complete =="));
}

void loop() {}
