/*
  ESP32 side of the UNO <-> ESP32 UART test.

  UART2:
    GPIO16 = RX  <- UNO D3 through the 5V -> 3.3V divider
    GPIO17 = TX  -> UNO D2 directly
    GND common with UNO

  USB Serial Monitor: 115200 baud
  ESP32 <-> UNO link: 9600 baud

  The UNO must run 03_uart_test.ino at the same time.
  Open a separate Serial Monitor for each board, both at 115200.
*/

const uint8_t UNO_RX_PIN = 16;  // ESP32 receives from UNO TX
const uint8_t UNO_TX_PIN = 17;  // ESP32 transmits to UNO RX

unsigned long lastSendMs = 0;
unsigned long sequenceNumber = 0;
String incomingLine;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, UNO_RX_PIN, UNO_TX_PIN);
  delay(600);

  Serial.println();
  Serial.println("=== ESP32 UART TEST ===");
  Serial.println("USB Serial: 115200");
  Serial.println("ESP32<->UNO link: 9600");
  Serial.println("GPIO16 = RX from divider, GPIO17 = TX to UNO D2");
  Serial.println();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSendMs >= 1000UL) {
    lastSendMs = now;
    sequenceNumber++;

    Serial.print("ESP_TX seq=");
    Serial.print(sequenceNumber);
    Serial.print(" ms=");
    Serial.println(now);

    Serial2.print("ESP_TX seq=");
    Serial2.print(sequenceNumber);
    Serial2.print(" ms=");
    Serial2.println(now);
  }

  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();

    if (c == '\n') {
      Serial.print("ESP_RX: ");
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
