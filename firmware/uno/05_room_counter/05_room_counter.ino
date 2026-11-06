/*
  Room / Queue Capacity Counter — Arduino UNO firmware

  The UNO owns the sensors, counting logic, LEDs and buzzer.
  The ESP32 only handles networking and the local dashboard.

  Wiring:
    Sensor A: TRIG D4, ECHO D5
    Sensor B: TRIG D6, ECHO D7
    Green LED: D8
    Red LED: D9
    Blue LED: D10
    Buzzer module I/O: D11 (active-low)
    ESP32 UART: UNO D2 = RX, UNO D3 = TX through divider

  UNO -> ESP32, newline-terminated:
    S,count,mode,capacity,in,out,distA,distB,threshold,flags,seq,millis
    E,seq,type,delta,count,millis
    A,verb,OK|ERR

  ESP32 -> UNO:
    C,MODE,ROOM|QUEUE
    C,CAP,1..99
    C,RESET
    C,CORRECT,0..999
    C,THRESH,10..300
    C,BEEP
    C,STATUS
*/

#include <SoftwareSerial.h>

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------
const uint8_t A_TRIG = 4;
const uint8_t A_ECHO = 5;
const uint8_t B_TRIG = 6;
const uint8_t B_ECHO = 7;

const uint8_t LED_GREEN = 8;
const uint8_t LED_RED = 9;
const uint8_t LED_BLUE = 10;
const uint8_t BUZZER_PIN = 11;

const uint8_t ESP_RX = 2;
const uint8_t ESP_TX = 3;

const uint8_t BUZZER_ON_LEVEL = LOW;   // confirmed 3-pin active-low module
const uint8_t BUZZER_OFF_LEVEL = HIGH;

SoftwareSerial espSerial(ESP_RX, ESP_TX);

// ---------------------------------------------------------------------------
// Configuration / state
// ---------------------------------------------------------------------------
enum Mode : uint8_t {
  MODE_ROOM = 0,
  MODE_QUEUE = 1
};

enum Sequence : uint8_t {
  SEQ_IDLE = 0,
  SEQ_A_FIRST = 1,
  SEQ_B_FIRST = 2,
  SEQ_WAIT_CLEAR = 3,
  SEQ_AMBIGUOUS = 4
};

const unsigned long SAMPLE_INTERVAL_MS = 60;
const unsigned long STATE_INTERVAL_MS = 500;
const unsigned long CROSS_TIMEOUT_MS = 1500;
const int DEFAULT_CAPACITY = 10;
const int DEFAULT_THRESHOLD_CM = 80;
const int CLEAR_HYSTERESIS_CM = 12;
const uint8_t NEAR_SAMPLES = 2;
const uint8_t CLEAR_SAMPLES = 3;

struct Sensor {
  uint8_t trigPin;
  uint8_t echoPin;
  bool active;
  bool fault;
  uint8_t nearCount;
  uint8_t clearCount;
  int distanceCm;
};

Sensor sensorA = {A_TRIG, A_ECHO, false, false, 0, 0, -1};
Sensor sensorB = {B_TRIG, B_ECHO, false, false, 0, 0, -1};

Mode mode = MODE_ROOM;
Sequence sequenceState = SEQ_IDLE;

int count = 0;
int capacity = DEFAULT_CAPACITY;
int thresholdCm = DEFAULT_THRESHOLD_CM;
int totalIn = 0;
int totalOut = 0;

unsigned long sequenceStartedMs = 0;
unsigned long lastSampleMs = 0;
unsigned long lastStateMs = 0;
unsigned long eventSequence = 0;
bool sampleSensorA = true;

// LED pulse deadlines
unsigned long greenUntilMs = 0;
unsigned long redUntilMs = 0;

// Non-blocking buzzer pattern
uint8_t beepRemaining = 0;
bool beepPhaseOn = false;
unsigned long beepNextMs = 0;
uint16_t beepOnMs = 100;
uint16_t beepOffMs = 90;

// UART command buffer
char commandBuffer[72];
uint8_t commandLength = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
bool isFull() {
  return capacity > 0 && count >= capacity;
}

int readDistanceCm(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
  if (duration == 0) {
    return -1;
  }

  // distance_cm = duration_us * 343 m/s / 2 / 10000
  int distance = (int)((duration * 343UL) / 20000UL);
  return distance;
}

void startBeep(uint8_t beeps, uint16_t onMs, uint16_t offMs) {
  beepRemaining = beeps;
  beepOnMs = onMs;
  beepOffMs = offMs;
  beepPhaseOn = true;
  digitalWrite(BUZZER_PIN, BUZZER_ON_LEVEL);
  beepNextMs = millis() + beepOnMs;
}

void updateBeep() {
  if (beepRemaining == 0) {
    digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);
    return;
  }

  if ((long)(millis() - beepNextMs) < 0) {
    return;
  }

  if (beepPhaseOn) {
    digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);
    beepPhaseOn = false;
    beepRemaining--;
    beepNextMs = millis() + beepOffMs;
  } else if (beepRemaining > 0) {
    digitalWrite(BUZZER_PIN, BUZZER_ON_LEVEL);
    beepPhaseOn = true;
    beepNextMs = millis() + beepOnMs;
  }
}

void pulseGreen() {
  greenUntilMs = millis() + 250;
}

void pulseRed() {
  redUntilMs = millis() + 250;
}

void updateLeds() {
  unsigned long now = millis();
  digitalWrite(LED_GREEN, now < greenUntilMs ? HIGH : LOW);
  digitalWrite(LED_RED, now < redUntilMs ? HIGH : LOW);
  digitalWrite(LED_BLUE, isFull() ? HIGH : LOW);
}

void emitEvent(const char *type, int delta) {
  eventSequence++;
  espSerial.print(F("E,"));
  espSerial.print(eventSequence);
  espSerial.print(',');
  espSerial.print(type);
  espSerial.print(',');
  espSerial.print(delta);
  espSerial.print(',');
  espSerial.print(count);
  espSerial.print(',');
  espSerial.println(millis());

  Serial.print(F("EVENT "));
  Serial.print(type);
  Serial.print(F(" count="));
  Serial.println(count);
}

void sendState() {
  uint8_t flags = 0;
  if (sensorA.active) flags |= 0x01;
  if (sensorB.active) flags |= 0x02;
  if (isFull()) flags |= 0x04;
  if (sensorA.fault) flags |= 0x08;
  if (sensorB.fault) flags |= 0x10;

  espSerial.print(F("S,"));
  espSerial.print(count);
  espSerial.print(',');
  espSerial.print((uint8_t)mode);
  espSerial.print(',');
  espSerial.print(capacity);
  espSerial.print(',');
  espSerial.print(totalIn);
  espSerial.print(',');
  espSerial.print(totalOut);
  espSerial.print(',');
  espSerial.print(sensorA.distanceCm);
  espSerial.print(',');
  espSerial.print(sensorB.distanceCm);
  espSerial.print(',');
  espSerial.print(thresholdCm);
  espSerial.print(',');
  espSerial.print(flags);
  espSerial.print(',');
  espSerial.print((uint8_t)sequenceState);
  espSerial.print(',');
  espSerial.println(millis());

  lastStateMs = millis();
}

void ack(const String &verb, bool ok) {
  espSerial.print(F("A,"));
  espSerial.print(verb);
  espSerial.print(',');
  espSerial.println(ok ? F("OK") : F("ERR"));
}

void noteFullIfNeeded(bool wasFull) {
  if (!wasFull && isFull()) {
    emitEvent("FULL", 0);
    startBeep(1, 500, 0);
  }
}

// ---------------------------------------------------------------------------
// Counting
// ---------------------------------------------------------------------------
void incrementCount() {
  bool wasFull = isFull();
  count++;
  totalIn++;
  emitEvent("IN", 1);
  pulseGreen();
  if (!isFull()) {
    startBeep(1, 110, 0);
  }
  noteFullIfNeeded(wasFull);
  sendState();
}

void decrementCount() {
  bool wasFull = isFull();
  totalOut++;
  if (count > 0) {
    count--;
    emitEvent("OUT", -1);
    pulseRed();
    startBeep(2, 85, 85);
  } else {
    emitEvent("INVALID", 0);
    startBeep(3, 55, 70);
  }
  noteFullIfNeeded(wasFull);
  sendState();
}

void roomEdge(bool sensorAEdge) {
  if (sequenceState == SEQ_WAIT_CLEAR || sequenceState == SEQ_AMBIGUOUS) {
    return;
  }

  if (sequenceState == SEQ_IDLE) {
    if (sensorAEdge && sensorB.active) {
      sequenceState = SEQ_AMBIGUOUS;
      emitEvent("INVALID", 0);
      return;
    }
    if (!sensorAEdge && sensorA.active) {
      sequenceState = SEQ_AMBIGUOUS;
      emitEvent("INVALID", 0);
      return;
    }

    sequenceState = sensorAEdge ? SEQ_A_FIRST : SEQ_B_FIRST;
    sequenceStartedMs = millis();
    return;
  }

  if (sequenceState == SEQ_A_FIRST && !sensorAEdge) {
    sequenceState = SEQ_WAIT_CLEAR;
    incrementCount();
    return;
  }

  if (sequenceState == SEQ_B_FIRST && sensorAEdge) {
    sequenceState = SEQ_WAIT_CLEAR;
    decrementCount();
  }
}

void queueEdge(bool sensorAEdge) {
  if (sensorAEdge) {
    incrementCount();
  } else {
    decrementCount();
  }
}

void onSensorEdge(bool sensorAEdge) {
  if (mode == MODE_ROOM) {
    roomEdge(sensorAEdge);
  } else {
    queueEdge(sensorAEdge);
  }
}

// ---------------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------------
void updateSensor(Sensor &sensor, bool isSensorA) {
  int distance = readDistanceCm(sensor.trigPin, sensor.echoPin);
  sensor.distanceCm = distance;

  if (distance < 0) {
    sensor.fault = true;
    sensor.nearCount = 0;
    sensor.clearCount = 0;
    return;
  }

  sensor.fault = false;

  bool near = distance <= thresholdCm;
  bool clear = distance >= thresholdCm + CLEAR_HYSTERESIS_CM;

  if (near) {
    sensor.clearCount = 0;
    if (sensor.nearCount < 255) sensor.nearCount++;
    if (!sensor.active && sensor.nearCount >= NEAR_SAMPLES) {
      sensor.active = true;
      onSensorEdge(isSensorA);
    }
  } else if (clear) {
    sensor.nearCount = 0;
    if (sensor.clearCount < 255) sensor.clearCount++;
    if (sensor.active && sensor.clearCount >= CLEAR_SAMPLES) {
      sensor.active = false;
    }
  } else {
    // In the hysteresis band: keep the current state.
    sensor.nearCount = 0;
    sensor.clearCount = 0;
  }
}

void updateSensors() {
  unsigned long now = millis();
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleMs = now;

  if (sampleSensorA) {
    updateSensor(sensorA, true);
  } else {
    updateSensor(sensorB, false);
  }
  sampleSensorA = !sampleSensorA;
}

void updateSequenceTimeout() {
  unsigned long now = millis();

  if ((sequenceState == SEQ_A_FIRST || sequenceState == SEQ_B_FIRST) &&
      now - sequenceStartedMs > CROSS_TIMEOUT_MS) {
    sequenceState = SEQ_IDLE;
    emitEvent("TIMEOUT", 0);
  }

  if ((sequenceState == SEQ_WAIT_CLEAR || sequenceState == SEQ_AMBIGUOUS) &&
      !sensorA.active && !sensorB.active) {
    sequenceState = SEQ_IDLE;
  }
}

// ---------------------------------------------------------------------------
// Commands from ESP32
// ---------------------------------------------------------------------------
void setMode(Mode newMode) {
  if (mode == newMode) return;
  mode = newMode;
  sequenceState = SEQ_IDLE;
  emitEvent("MODE", (int)mode);
  sendState();
}

void setCapacity(int newCapacity) {
  newCapacity = constrain(newCapacity, 1, 99);
  bool wasFull = isFull();
  capacity = newCapacity;
  emitEvent("CAP", capacity);
  noteFullIfNeeded(wasFull);
  sendState();
}

void setThreshold(int newThreshold) {
  thresholdCm = constrain(newThreshold, 10, 300);
  emitEvent("THRESH", thresholdCm);
  sendState();
}

void resetCount() {
  count = 0;
  emitEvent("RESET", 0);
  sendState();
}

void correctCount(int newCount) {
  int oldCount = count;
  count = constrain(newCount, 0, 999);
  emitEvent("CORRECT", count - oldCount);
  sendState();
}

void handleCommand(String line) {
  line.trim();
  if (!line.startsWith("C,")) {
    return;
  }

  String rest = line.substring(2);
  int comma = rest.indexOf(',');
  String verb = comma < 0 ? rest : rest.substring(0, comma);
  String value = comma < 0 ? "" : rest.substring(comma + 1);
  verb.trim();
  value.trim();
  verb.toUpperCase();

  bool ok = false;

  if (verb == "MODE") {
    value.toUpperCase();
    if (value == "ROOM") {
      setMode(MODE_ROOM);
      ok = true;
    } else if (value == "QUEUE") {
      setMode(MODE_QUEUE);
      ok = true;
    }
  } else if (verb == "CAP") {
    int v = value.toInt();
    if (v >= 1 && v <= 99) {
      setCapacity(v);
      ok = true;
    }
  } else if (verb == "RESET") {
    resetCount();
    ok = true;
  } else if (verb == "CORRECT") {
    int v = value.toInt();
    if (v >= 0 && v <= 999) {
      correctCount(v);
      ok = true;
    }
  } else if (verb == "THRESH") {
    int v = value.toInt();
    if (v >= 10 && v <= 300) {
      setThreshold(v);
      ok = true;
    }
  } else if (verb == "BEEP") {
    startBeep(2, 100, 80);
    ok = true;
  } else if (verb == "STATUS") {
    sendState();
    ok = true;
  }

  ack(verb, ok);
}

void readCommands() {
  while (espSerial.available() > 0) {
    char c = (char)espSerial.read();
    if (c == '\n') {
      commandBuffer[commandLength] = '\0';
      if (commandLength > 0) {
        handleCommand(String(commandBuffer));
      }
      commandLength = 0;
    } else if (c != '\r') {
      if (commandLength < sizeof(commandBuffer) - 1) {
        commandBuffer[commandLength++] = c;
      } else {
        commandLength = 0;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);

  pinMode(A_TRIG, OUTPUT);
  pinMode(A_ECHO, INPUT);
  pinMode(B_TRIG, OUTPUT);
  pinMode(B_ECHO, INPUT);
  digitalWrite(A_TRIG, LOW);
  digitalWrite(B_TRIG, LOW);

  Serial.begin(115200);
  espSerial.begin(9600);

  delay(300);
  Serial.println(F("Room/queue counter ready"));
  sendState();
}

void loop() {
  unsigned long now = millis();

  updateSensors();
  updateSequenceTimeout();
  updateBeep();
  updateLeds();
  readCommands();

  if (now - lastStateMs >= STATE_INTERVAL_MS) {
    sendState();
  }
}
