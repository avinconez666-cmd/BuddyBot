const uint8_t PINS[] = {
  2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
  14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
  35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53
};
const uint8_t PIN_COUNT = sizeof(PINS) / sizeof(PINS[0]);
const uint8_t SAMPLES = 5;

long measureCm(int trig, int echo) {
  pinMode(trig, OUTPUT);
  pinMode(echo, INPUT);
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long dur = pulseIn(echo, HIGH, 30000);
  if (dur == 0) return -1;
  long cm = (long)((dur * 0.034f) / 2.0f);
  if (cm < 2 || cm > 400) return -1;
  return cm;
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println(F("{\"scan\":\"START\"}"));

  for (uint8_t ei = 0; ei < PIN_COUNT; ei++) {
    int echo = PINS[ei];
    int bestTrig = -1;
    long bestCm = -1;
    uint8_t bestOk = 0;
    long bestSpread = 9999;

    for (uint8_t ti = 0; ti < PIN_COUNT; ti++) {
      int trig = PINS[ti];
      if (trig == echo) continue;

      long minV = 9999, maxV = 0, sum = 0;
      uint8_t ok = 0;
      for (uint8_t s = 0; s < SAMPLES; s++) {
        long d = measureCm(trig, echo);
        if (d > 0) {
          ok++;
          sum += d;
          if (d < minV) minV = d;
          if (d > maxV) maxV = d;
        }
        delay(25);
      }

      if (ok < 4) continue;
      long spread = maxV - minV;
      if (spread > 8) continue;

      if (ok > bestOk || (ok == bestOk && spread < bestSpread)) {
        bestTrig = trig;
        bestCm = sum / ok;
        bestOk = ok;
        bestSpread = spread;
      }
    }

    if (bestTrig >= 0) {
      Serial.print(F("{\"trig\":"));
      Serial.print(bestTrig);
      Serial.print(F(",\"echo\":"));
      Serial.print(echo);
      Serial.print(F(",\"cm\":"));
      Serial.print(bestCm);
      Serial.print(F(",\"spread\":"));
      Serial.print(bestSpread);
      Serial.println(F("}"));
    }
  }

  Serial.println(F("{\"scan\":\"DONE\"}"));
}

void loop() {
  delay(10000);
}