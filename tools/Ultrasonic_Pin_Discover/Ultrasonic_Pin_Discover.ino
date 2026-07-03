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
}

void loop() {
  static bool done = false;
  if (done) { delay(10000); return; }

  for (uint8_t ti = 0; ti < PIN_COUNT; ti++) {
    int trig = PINS[ti];
    for (uint8_t ei = 0; ei < PIN_COUNT; ei++) {
      int echo = PINS[ei];
      if (trig == echo) continue;

      long sum = 0;
      uint8_t ok = 0;
      long minV = 9999, maxV = 0;

      for (uint8_t s = 0; s < SAMPLES; s++) {
        long d = measureCm(trig, echo);
        if (d > 0) {
          ok++;
          sum += d;
          if (d < minV) minV = d;
          if (d > maxV) maxV = d;
        }
        delay(40);
      }

      if (ok >= 4 && (maxV - minV) <= 8) {
        Serial.print(F("{\"trig\":"));
        Serial.print(trig);
        Serial.print(F(",\"echo\":"));
        Serial.print(echo);
        Serial.print(F(",\"cm\":"));
        Serial.print(sum / ok);
        Serial.print(F(",\"ok\":"));
        Serial.print(ok);
        Serial.println(F("}"));
      }
    }
  }

  Serial.println(F("{\"scan\":\"DONE\"}"));
  done = true;
}