/*
 * Ultrasonic_Pin_Scan — HC-SR04 TRIG/ECHO discovery for Mega 2560
 * Upload via USB, open Serial Monitor at 115200 baud.
 * Output: JSON lines for each detected TRIG/ECHO pair.
 */

const uint8_t PINS[] = {
  2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
  14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
  35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53
};
const uint8_t PIN_COUNT = sizeof(PINS) / sizeof(PINS[0]);

const unsigned long PULSE_TIMEOUT_US = 6000;   // fast scan (~100 cm max)
const uint8_t SAMPLES = 2;
const long MIN_CM = 2;
const long MAX_CM = 400;

long measureCm(int trig, int echo) {
  pinMode(trig, OUTPUT);
  pinMode(echo, INPUT);
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  unsigned long dur = pulseIn(echo, HIGH, PULSE_TIMEOUT_US);
  if (dur == 0) return -1;

  long cm = (long)((dur * 0.034f) / 2.0f);
  if (cm < MIN_CM || cm > MAX_CM) return -1;
  return cm;
}

bool testPair(int trig, int echo, long &avgCm, uint8_t &goodSamples) {
  long sum = 0;
  goodSamples = 0;

  for (uint8_t i = 0; i < SAMPLES; i++) {
    long d = measureCm(trig, echo);
    if (d > 0) {
      sum += d;
      goodSamples++;
    }
    delay(25);
  }

  if (goodSamples == 0) return false;
  avgCm = sum / goodSamples;
  return true;
}

void emitJson(const __FlashStringHelper *key, const char *value) {
  Serial.print(F("{\""));
  Serial.print(key);
  Serial.print(F("\":\""));
  Serial.print(value);
  Serial.println(F("\"}"));
}

void setup() {
  Serial.begin(115200);
  delay(800);
  emitJson(F("scan"), "START");
}

void loop() {
  static bool finished = false;
  if (finished) {
    delay(10000);
    return;
  }

  uint8_t hits = 0;
  emitJson(F("scan"), "RUNNING");

  for (uint8_t ti = 0; ti < PIN_COUNT; ti++) {
    int trig = PINS[ti];

    for (uint8_t ei = 0; ei < PIN_COUNT; ei++) {
      int echo = PINS[ei];
      if (trig == echo) continue;

      long avgCm = 0;
      uint8_t good = 0;
      if (!testPair(trig, echo, avgCm, good)) continue;

      hits++;
      Serial.print(F("{\"hit\":true,\"trig\":"));
      Serial.print(trig);
      Serial.print(F(",\"echo\":"));
      Serial.print(echo);
      Serial.print(F(",\"dist_cm\":"));
      Serial.print(avgCm);
      Serial.print(F(",\"samples_ok\":"));
      Serial.print(good);
      Serial.println(F("}"));
    }

    if ((ti % 6) == 0) {
      Serial.print(F("{\"progress_pct\":"));
      Serial.print((100UL * ti) / PIN_COUNT);
      Serial.println(F("}"));
    }
  }

  Serial.print(F("{\"scan\":\"DONE\",\"hits\":"));
  Serial.print(hits);
  Serial.println(F("}"));
  finished = true;
}