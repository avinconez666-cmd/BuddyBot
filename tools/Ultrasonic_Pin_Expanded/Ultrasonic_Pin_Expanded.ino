/*
 * Expanded confirmation for BuddyBot + high-signal brute-force pairs.
 */

struct Pair { const char *name; int trig; int echo; };

const Pair CANDIDATES[] = {
  {"FRONT_V33",      45, 47}, {"FRONT_V33_SW",  47, 45},
  {"LEFT_V33",       35, 37}, {"LEFT_V33_SW",   37, 35},
  {"RIGHT_V33",      39, 41}, {"RIGHT_V33_SW",  41, 39},
  {"REAR_V33",       49, 51}, {"REAR_V33_SW",   51, 49},
  {"FRONT_V31",      46, 49}, {"FRONT_V31_SW",  49, 46},
  {"LEFT_V31",       28, 29}, {"LEFT_V31_SW",   29, 28},
  {"RIGHT_V31",      38, 40}, {"RIGHT_V31_SW",  40, 38},
  {"REAR_V31",       51, 47}, {"REAR_V31_SW",   47, 51},
  {"SCAN_20_34",     20, 34}, {"SCAN_29_27",    29, 27},
  {"SCAN_48_50",     48, 50}, {"SCAN_50_28",    50, 28},
  {"SCAN_37_39",     37, 39}, {"SCAN_42_28",    42, 28},
};
const uint8_t CANDIDATE_COUNT = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

const uint8_t READINGS = 10;
const unsigned long PULSE_TIMEOUT_US = 30000;

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
  if (cm < 2 || cm > 400) return -1;
  return cm;
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println(F("{\"expanded\":\"START\"}"));
}

void loop() {
  static bool done = false;
  if (done) { delay(10000); return; }

  for (uint8_t i = 0; i < CANDIDATE_COUNT; i++) {
    const Pair &p = CANDIDATES[i];
    long minV = 9999, maxV = 0;
    uint8_t ok = 0;
    long sum = 0;

    for (uint8_t r = 0; r < READINGS; r++) {
      long v = measureCm(p.trig, p.echo);
      if (v > 0) { ok++; sum += v; if (v < minV) minV = v; if (v > maxV) maxV = v; }
      delay(70);
    }

    Serial.print(F("{\"name\":\"")); Serial.print(p.name);
    Serial.print(F("\",\"trig\":")); Serial.print(p.trig);
    Serial.print(F(",\"echo\":")); Serial.print(p.echo);
    Serial.print(F(",\"ok\":")); Serial.print(ok);
    Serial.print(F(",\"avg\":")); Serial.print(ok ? sum / ok : -1);
    Serial.print(F(",\"spread\":")); Serial.print(ok ? maxV - minV : -1);
    Serial.print(F(",\"valid\":")); Serial.print(ok >= 7 && (maxV - minV) <= 20);
    Serial.println(F("}"));
  }

  Serial.println(F("{\"expanded\":\"DONE\"}"));
  done = true;
}