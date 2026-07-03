/*
 * Confirm likely HC-SR04 pairs with repeated stable readings.
 */

struct Pair {
  const char *name;
  int trig;
  int echo;
};

const Pair CANDIDATES[] = {
  {"FRONT_V33", 45, 47},
  {"LEFT_V33",  35, 37},
  {"RIGHT_V33", 39, 41},
  {"REAR_V33",  49, 51},
  {"FRONT_V31", 46, 49},
  {"LEFT_V31",  28, 29},
  {"RIGHT_V31", 38, 40},
  {"REAR_V31",  51, 47},
  {"REAR_ALT",  49, 51},
  {"ECHO51_ANY", 49, 51},
};
const uint8_t CANDIDATE_COUNT = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

const uint8_t READINGS = 8;
const unsigned long PULSE_TIMEOUT_US = 25000;

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
  Serial.println(F("{\"confirm\":\"START\"}"));
}

void loop() {
  static bool done = false;
  if (done) {
    delay(10000);
    return;
  }

  for (uint8_t i = 0; i < CANDIDATE_COUNT; i++) {
    const Pair &p = CANDIDATES[i];
    long vals[READINGS];
    uint8_t ok = 0;
    long sum = 0;

    for (uint8_t r = 0; r < READINGS; r++) {
      vals[r] = measureCm(p.trig, p.echo);
      if (vals[r] > 0) {
        ok++;
        sum += vals[r];
      }
      delay(60);
    }

    long minV = 9999;
    long maxV = 0;
    for (uint8_t r = 0; r < READINGS; r++) {
      if (vals[r] > 0) {
        if (vals[r] < minV) minV = vals[r];
        if (vals[r] > maxV) maxV = vals[r];
      }
    }

    Serial.print(F("{\"name\":\""));
    Serial.print(p.name);
    Serial.print(F("\",\"trig\":"));
    Serial.print(p.trig);
    Serial.print(F(",\"echo\":"));
    Serial.print(p.echo);
    Serial.print(F(",\"ok\":"));
    Serial.print(ok);
    Serial.print(F(",\"avg\":"));
    Serial.print(ok ? (sum / ok) : -1);
    Serial.print(F(",\"spread\":"));
    Serial.print(ok ? (maxV - minV) : -1);
    Serial.print(F(",\"valid\":"));
    Serial.print(ok >= 5 && (maxV - minV) <= 15 ? "true" : "false");
    Serial.println(F("}"));
  }

  Serial.println(F("{\"confirm\":\"DONE\"}"));
  done = true;
}