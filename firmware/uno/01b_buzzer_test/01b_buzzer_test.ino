/*
  Dedicated test for the 3-pin active buzzer module on D11.

  From the Lab 5 notes and working sketch, the module is:
    VCC / GND / I-O, ACTIVE-LOW
    I/O LOW  -> sound
    I/O HIGH -> silent

  This sketch holds the buzzer ON for 2 seconds and OFF for 2 seconds,
  so it is much easier to hear than the short beep in the full output test.

  Serial Monitor: 115200 baud
*/

const uint8_t BUZZER_PIN = 11;

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);

  // Active-low module: HIGH is the safe/off state at boot.
  digitalWrite(BUZZER_PIN, HIGH);

  Serial.begin(115200);
  delay(600);
  Serial.println();
  Serial.println("=== D11 ACTIVE-LOW BUZZER TEST ===");
  Serial.println("Expected: 2 seconds ON, 2 seconds OFF, repeating.");
  Serial.println("VCC must be 5V, GND must be GND, I/O must be D11.");
  Serial.println();
}

void loop() {
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("D11 = LOW  -> buzzer should sound");
  delay(2000);

  digitalWrite(BUZZER_PIN, HIGH);
  Serial.println("D11 = HIGH -> buzzer should be silent");
  delay(2000);
}
