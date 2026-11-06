/*
  UNO pin-pair loopback test for the HC-SR04 signal wiring.

  This tests the UNO pins and breadboard rows, not the sensors.

  PROCEDURE:
    1. Disconnect both HC-SR04 sensors.
    2. Put a jumper wire directly between D4 and D5.
    3. Run this sketch. Pair 1 should report HIGH=1 and LOW=0.
    4. Move the jumper to D6 and D7. Pair 2 should report HIGH=1 and LOW=0.
    5. Remove the jumper before reconnecting the sensors.

  Serial Monitor: 115200 baud
*/

struct Pair {
  uint8_t trigPin;
  uint8_t echoPin;
  const char *name;
};

Pair pairs[] = {
  {4, 5, "D4/D5 (sensor A pins)"},
  {6, 7, "D6/D7 (sensor B pins)"}
};

void testPair(const Pair &p) {
  pinMode(p.trigPin, OUTPUT);
  pinMode(p.echoPin, INPUT);

  digitalWrite(p.trigPin, HIGH);
  delayMicroseconds(10);
  int highLevel = digitalRead(p.echoPin);

  digitalWrite(p.trigPin, LOW);
  delayMicroseconds(10);
  int lowLevel = digitalRead(p.echoPin);

  Serial.print(p.name);
  Serial.print(": HIGH=");
  Serial.print(highLevel);
  Serial.print("  LOW=");
  Serial.println(lowLevel);
}

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println();
  Serial.println("=== UNO PIN-PAIR LOOPBACK TEST ===");
  Serial.println("Disconnect both sensors first.");
  Serial.println("Jumper D4-D5, then D6-D7.");
  Serial.println("Expected for a good pair: HIGH=1  LOW=0");
  Serial.println();
}

void loop() {
  for (const Pair &p : pairs) {
    testPair(p);
  }
  Serial.println();
  delay(1500);
}
