struct Pair { const char *name; int trig; int echo; };

const Pair SENSORS[] = {
  {"P_37_39", 37, 39},
  {"P_8_51",   8, 51},
  {"P_49_51", 49, 51},
  {"P_48_50", 48, 50},
  {"P_47_49", 47, 49},
};
const uint8_t N = sizeof(SENSORS) / sizeof(SENSORS[0]);

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
  delay(500);
}

void loop() {
  Serial.print(F("{\"t\":"));
  Serial.print(millis());
  for (uint8_t i = 0; i < N; i++) {
    Serial.print(F(",\""));
    Serial.print(SENSORS[i].name);
    Serial.print(F("\":"));
    Serial.print(measureCm(SENSORS[i].trig, SENSORS[i].echo));
  }
  Serial.println(F("}"));
  delay(400);
}