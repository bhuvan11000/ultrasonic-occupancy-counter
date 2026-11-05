/*
  Arduino UNO output test
  Tests: green LED (D8), red LED (D9), blue LED (D10), buzzer module I/O (D11)

  Serial Monitor: 115200 baud

  IMPORTANT:
  - Each bare LED must have a 220-330 ohm resistor in series.
  - The 3-pin buzzer module connects VCC -> 5V, GND -> GND, I/O -> D11.
  - This sketch tests both HIGH and LOW on the buzzer I/O so you can discover
    whether the module is active-high or active-low.
*/

const uint8_t GREEN_PIN = 8;
const uint8_t RED_PIN = 9;
const uint8_t BLUE_PIN = 10;
const uint8_t BUZZER_PIN = 11;

void ledsOff() {
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(RED_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);
}

void testLed(const char *name, uint8_t pin) {
  ledsOff();
  digitalWrite(pin, HIGH);
  Serial.print(name);
  Serial.println(" LED ON");
  delay(600);
  digitalWrite(pin, LOW);
  Serial.print(name);
  Serial.println(" LED OFF");
  delay(250);
}

void setup() {
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  ledsOff();

  // Keep the buzzer input high-impedance until we deliberately test it.
  pinMode(BUZZER_PIN, INPUT);

  Serial.begin(115200);
  delay(600);

  Serial.println();
  Serial.println("=== UNO OUTPUT TEST ===");
  Serial.println("LEDs: D8 green, D9 red, D10 blue");
  Serial.println("Buzzer module: VCC=5V, GND=GND, I/O=D11");
  Serial.println();
  Serial.println("Watch which buzzer test produces the sound:");
  Serial.println("  HIGH beep  -> module is active-high");
  Serial.println("  LOW beep   -> module is active-low");
  Serial.println();
}

void loop() {
  Serial.println("--- LED sequence ---");
  testLed("GREEN", GREEN_PIN);
  testLed("RED", RED_PIN);
  testLed("BLUE", BLUE_PIN);

  ledsOff();
  digitalWrite(GREEN_PIN, HIGH);
  digitalWrite(RED_PIN, HIGH);
  digitalWrite(BLUE_PIN, HIGH);
  Serial.println("All three LEDs ON");
  delay(700);
  ledsOff();
  Serial.println("All three LEDs OFF");
  delay(500);

  Serial.println("--- buzzer test ---");
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, HIGH);
  Serial.println("Buzzer I/O = HIGH for 400 ms");
  delay(400);

  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("Buzzer I/O = LOW for 400 ms");
  delay(400);

  // High-impedance should leave the module silent.
  pinMode(BUZZER_PIN, INPUT);
  Serial.println("Buzzer I/O = high-Z; module should be silent");
  Serial.println();
  delay(1600);
}
