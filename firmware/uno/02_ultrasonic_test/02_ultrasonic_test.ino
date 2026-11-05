/*
  Arduino UNO ultrasonic sensor test
  Sensor A: TRIG D4, ECHO D5
  Sensor B: TRIG D6, ECHO D7

  Serial Monitor: 115200 baud

  The sketch reads the sensors alternately to reduce ultrasonic crosstalk.
  Move a flat object (a book or your hand) in front of each sensor.
*/

const uint8_t A_TRIG = 4;
const uint8_t A_ECHO = 5;
const uint8_t B_TRIG = 6;
const uint8_t B_ECHO = 7;

const unsigned long ECHO_TIMEOUT_US = 30000UL;  // about 5 m maximum
const float CM_PER_US = 0.0343f;                // speed of sound, 20 C

float readDistanceCm(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) {
    return -1.0f;  // no echo / out of range / wiring problem
  }

  return (duration * CM_PER_US) / 2.0f;
}

void printDistance(const char *label, float distance) {
  Serial.print(label);
  Serial.print(": ");
  if (distance < 0) {
    Serial.print("---");
  } else {
    Serial.print(distance, 1);
    Serial.print(" cm");
  }
}

void setup() {
  pinMode(A_TRIG, OUTPUT);
  pinMode(A_ECHO, INPUT);
  pinMode(B_TRIG, OUTPUT);
  pinMode(B_ECHO, INPUT);

  digitalWrite(A_TRIG, LOW);
  digitalWrite(B_TRIG, LOW);

  Serial.begin(115200);
  delay(600);
  Serial.println();
  Serial.println("=== UNO ULTRASONIC TEST ===");
  Serial.println("A: TRIG D4 / ECHO D5");
  Serial.println("B: TRIG D6 / ECHO D7");
  Serial.println("Move a flat object 10-50 cm in front of each sensor.");
  Serial.println();
}

void loop() {
  float distanceA = readDistanceCm(A_TRIG, A_ECHO);
  delay(60);  // let the previous ping die away before firing the other sensor
  float distanceB = readDistanceCm(B_TRIG, B_ECHO);

  printDistance("A", distanceA);
  Serial.print("   |   ");
  printDistance("B", distanceB);
  Serial.println();

  delay(250);
}
