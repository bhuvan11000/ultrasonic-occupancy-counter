/*
  Buzzer diagnostic for D11.

  This distinguishes:
    - active-low module   -> sounds during DC LOW
    - active-high module  -> sounds during DC HIGH
    - passive buzzer      -> sounds during the 2 kHz tone, but only clicks on DC

  The onboard D13 LED mirrors the D11 drive level so you can see whether the
  UNO pin is actually changing even if the buzzer is silent.

  Serial Monitor: 115200 baud
*/

const uint8_t BUZZER_PIN = 11;

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  // Start in the likely-off state for the known active-low module.
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(600);
  Serial.println();
  Serial.println("=== BUZZER DIAGNOSTIC: D11 ===");
  Serial.println("D13 mirrors D11. Watch both the LED and the buzzer.");
  Serial.println("VCC must be 5V, GND must be GND, I/O must be D11.");
  Serial.println();
}

void loop() {
  Serial.println("1) DC LOW for 2 s -> active-low module should sound");
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_BUILTIN, LOW);
  delay(2000);

  Serial.println("2) DC HIGH for 2 s -> active-high module should sound");
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(2000);

  Serial.println("3) 2 kHz tone for 2 s -> passive buzzer should sound");
  tone(BUZZER_PIN, 2000);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(2000);
  noTone(BUZZER_PIN);

  // High-impedance is the safest state when the polarity is unknown.
  pinMode(BUZZER_PIN, INPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("Buzzer pin set to high-Z for 1 s");
  Serial.println();
  delay(1000);

  pinMode(BUZZER_PIN, OUTPUT);
}
