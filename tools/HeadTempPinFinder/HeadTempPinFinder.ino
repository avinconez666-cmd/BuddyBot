/*
 * ═══════════════════════════════════════════════════════════════════════
 *  BuddyBot — Head Temperature Sensor Pin Finder
 * ═══════════════════════════════════════════════════════════════════════
 *  Board target: arduino:avr:mega  (KS0509 Mega 2560)
 *
 *  PURPOSE
 *  ────────
 *  Scans all 16 analog pins (A0–A15) on the Mega 2560 and applies the
 *  same NTC thermistor formula used in BuddyBot Mega V37:
 *
 *    NTC 10kΩ, B=3950, 10kΩ pull-up to 5V
 *    V_adc  = raw * (5.0 / 1023.0)
 *    R_ntc  = (5.0 - V_adc) / V_adc * 10000
 *    T_kelvin = 1 / ( ln(R_ntc / 10000) / 3950 + 1/298.15 )
 *    T_celsius = T_kelvin - 273.15
 *
 *  HOW TO USE
 *  ───────────
 *  1. Upload to the KS0509 Mega (disconnect Pico W and other Serial1
 *     devices first — this sketch owns the bus for diagnostics).
 *  2. Open Serial Monitor at 115200 baud.
 *  3. Every 3 seconds a full table of all 16 pins is printed.
 *  4. Look for a pin that:
 *       ✅ Shows a STABLE reading (not jumping wildly each cycle)
 *       ✅ Temperature is between 15°C and 45°C (realistic room/device temp)
 *       ✅ Temperature changes slowly when you hold your finger near the
 *          sensor (gently warm it) — it will rise 2–5°C over 5–10 seconds
 *  5. Ignore pins that show:
 *       ❌ ADC raw = 0 or 1023 every time (disconnected/short)
 *       ❌ "NTC" column says "---" or "OUT_OF_RANGE" (not a thermistor)
 *       ❌ Wildly different reading each cycle (floating pin)
 *
 *  KNOWN ASSIGNMENTS (from V37 — these may help eliminate candidates):
 *    A3  = ACS712 current sensor (outputs ~2.5V at 0A → raw ~512)
 *    A5  = MQ2 gas sensor analog output
 *    A7  = Battery temperature thermistor
 *    A9  = Battery voltage divider
 *    A10 = LDR light sensor
 *    A12 = Sound sensor
 *    A13 = HEAD_TEMP_SENSOR (current V37 assignment — may or may not be right)
 * ═══════════════════════════════════════════════════════════════════════
 */

#include <math.h>

// NTC parameters matching V37
#define NTC_NOMINAL_R   10000.0f  // 10kΩ nominal resistance
#define NTC_PULLUP_R    10000.0f  // 10kΩ pull-up to 5V
#define NTC_B_COEFF     3950.0f   // B coefficient
#define NTC_NOMINAL_T   298.15f   // nominal temp in Kelvin (25°C)
#define ADC_VREF        5.0f
#define ADC_MAX         1023.0f

// How many samples to average per pin (reduces single-read noise)
#define SAMPLES_PER_PIN 8
#define SAMPLE_DELAY_MS 2

// Temperature range considered a plausible NTC reading
#define TEMP_MIN  10.0f
#define TEMP_MAX  80.0f

// Variance threshold — pin is "stable" if max-min across samples < this
#define STABILITY_THRESHOLD 2  // ADC counts

// ─────────────────────────────────────────────────────────────────────
float ntcTemperature(int rawAvg) {
  if (rawAvg <= 5 || rawAvg >= 1018) return -999.0f;  // open/shorted
  float v = (rawAvg / ADC_MAX) * ADC_VREF;
  float r = (ADC_VREF - v) / v * NTC_PULLUP_R;
  if (r <= 0) return -999.0f;
  float s = logf(r / NTC_NOMINAL_R) / NTC_B_COEFF + 1.0f / NTC_NOMINAL_T;
  float c = (1.0f / s) - 273.15f;
  return (c < -60.0f || c > 200.0f) ? -999.0f : c;
}

// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(500);

  Serial.println();
  Serial.println(F("╔══════════════════════════════════════════════════════════╗"));
  Serial.println(F("║  BuddyBot — Head Temp Sensor Pin Finder                 ║"));
  Serial.println(F("║  Board: KS0509 Mega 2560  |  Baud: 115200               ║"));
  Serial.println(F("╚══════════════════════════════════════════════════════════╝"));
  Serial.println();
  Serial.println(F("Scanning A0–A15 every 3 seconds."));
  Serial.println(F("Look for a STABLE pin reading 15–45°C that warms"));
  Serial.println(F("when you hold a finger near the sensor."));
  Serial.println();
}

// ─────────────────────────────────────────────────────────────────────
void loop() {
  Serial.println(F("─────────────────────────────────────────────────────────────"));
  Serial.println(F(" Pin   Raw(avg)  Stability  Temp(°C)   Assessment"));
  Serial.println(F("─────────────────────────────────────────────────────────────"));

  int bestPin = -1;
  float bestTemp = -999.0f;

  for (int a = 0; a <= 15; a++) {
    int pin = A0 + a;

    // Take multiple samples
    int readings[SAMPLES_PER_PIN];
    long sum = 0;
    int minR = 1023, maxR = 0;
    for (int s = 0; s < SAMPLES_PER_PIN; s++) {
      int r = analogRead(pin);
      readings[s] = r;
      sum += r;
      if (r < minR) minR = r;
      if (r > maxR) maxR = r;
      delay(SAMPLE_DELAY_MS);
    }
    int avg = (int)(sum / SAMPLES_PER_PIN);
    int spread = maxR - minR;   // stability metric (lower = more stable)

    float temp = ntcTemperature(avg);
    bool plausible = (temp > TEMP_MIN && temp < TEMP_MAX);
    bool stable    = (spread <= STABILITY_THRESHOLD);

    // Format output
    char buf[80];
    char tempStr[12];
    char stableStr[10];
    char assessStr[30];

    if (spread <= 2)        snprintf(stableStr, sizeof(stableStr), "ROCK");
    else if (spread <= 8)   snprintf(stableStr, sizeof(stableStr), "good");
    else if (spread <= 30)  snprintf(stableStr, sizeof(stableStr), "noisy");
    else                    snprintf(stableStr, sizeof(stableStr), "float?");

    // NOTE: AVR snprintf does NOT support %f without special linker flags.
    // Use dtostrf() — the standard AVR float-to-string function.
    if (temp <= -999.0f) {
      strncpy(tempStr, "   ---  ", sizeof(tempStr));
    } else {
      char tmp[10];
      dtostrf(temp, 6, 1, tmp);
      snprintf(tempStr, sizeof(tempStr), "%s", tmp);
    }

    if      (avg <= 5)      snprintf(assessStr, sizeof(assessStr), "OPEN CIRCUIT");
    else if (avg >= 1018)   snprintf(assessStr, sizeof(assessStr), "SHORT TO GND");
    else if (plausible && stable)  snprintf(assessStr, sizeof(assessStr), "<<< LIKELY NTC <<<");
    else if (plausible)     snprintf(assessStr, sizeof(assessStr), "possible (noisy)");
    else                    snprintf(assessStr, sizeof(assessStr), "not a thermistor");

    snprintf(buf, sizeof(buf), " A%-2d   %4d      ±%-3d      %s  %s",
             a, avg, spread, tempStr, assessStr);
    Serial.println(buf);

    // Track best candidate
    if (plausible && stable && temp > bestTemp) {
      bestPin  = a;
      bestTemp = temp;
    }

    delay(5);
  }

  Serial.println(F("─────────────────────────────────────────────────────────────"));
  if (bestPin >= 0) {
    Serial.print(F("  BEST CANDIDATE: A"));
    Serial.print(bestPin);
    Serial.print(F("  ("));
    Serial.print(bestTemp, 1);
    Serial.println(F("°C)  — hold finger near sensor to confirm"));
    Serial.print(F("  If confirmed, set in V37: #define HEAD_TEMP_SENSOR  A"));
    Serial.println(bestPin);
  } else {
    Serial.println(F("  No clear NTC candidate found this cycle — check wiring."));
    Serial.println(F("  NTC needs: one lead to this analog pin, other lead to GND,"));
    Serial.println(F("  and a 10k resistor from the analog pin to 5V (pull-up)."));
  }
  Serial.println();

  delay(3000);
}
