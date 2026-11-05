/*
  UNO side of the UNO <-> ESP32 UART test.

  UNO software serial:
    D2 = RX  <- ESP32 GPIO17 / TX2
    D3 = TX  -> 1k -> node -> ESP32 GPIO16 / RX2
    node -> 1k -> 220R -> GND

  USB Serial Monitor: 115200 baud
  UNO <-> ESP32 link: 9600 baud

  The ESP32 must run 01_uart_test.ino at the same time.
  Open a separate Serial Monitor for each board, both at 115200.
*/

#include <SoftwareSerial.h>

const uint8_t ESP_RX_PIN = 2;  // UNO receives from ESP32 TX
const uint8_t ESP_TX_PIN = 3;  // UNO transmits through the divider

SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);

unsigned long lastSendMs = 0;
unsigned long sequenceNumber = 0;
String incomingLine;

void setup() {
  Serial.begin(115200);
  espSerial.begin(9600);
  delay(600);

  Serial.println();
  Serial.println("=== UNO UART TEST ===");
  Serial.println("USB Serial: 115200");
  Serial.println("UNO<->ESP link: 9600");
  Serial.println("UNO D2 = RX, UNO D3 = TX through divider");
  Serial.println();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSendMs >= 1000UL) {
    lastSendMs = now;
    sequenceNumber++;

    Serial.print("UNO_TX seq=");
    Serial.print(sequenceNumber);
    Serial.print(" ms=");
    Serial.println(now);

    espSerial.print("UNO_TX seq=");
    espSerial.print(sequenceNumber);
    espSerial.print(" ms=");
    espSerial.println(now);
  }

  while (espSerial.available() > 0) {
    char c = (char)espSerial.read();

    if (c == '\n') {
      Serial.print("UNO_RX: ");
      Serial.println(incomingLine);
      incomingLine = "";
    } else if (c != '\r') {
      if (incomingLine.length() < 120) {
        incomingLine += c;
      } else {
        incomingLine = "";
      }
    }
  }
}
