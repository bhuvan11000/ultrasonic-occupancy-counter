/*
  ESP32 Wi-Fi scan test
  Verifies that the ESP32 can see 2.4 GHz Wi-Fi networks.

  Serial Monitor: 115200 baud
  This does not need your Wi-Fi password yet.
*/

#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(600);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);

  Serial.println();
  Serial.println("=== ESP32 WI-FI SCAN ===");
  Serial.println("If networks appear below, the radio and antenna are working.");
  Serial.println();
}

void loop() {
  Serial.println("Scanning...");
  int count = WiFi.scanNetworks();

  if (count <= 0) {
    Serial.println("No networks found. Check the antenna/board and try again.");
  } else {
    Serial.print("Networks found: ");
    Serial.println(count);

    for (int i = 0; i < count; i++) {
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print("  RSSI=");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" dBm  channel=");
      Serial.print(WiFi.channel(i));
      Serial.print("  ");
      Serial.println(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "encrypted");
      delay(10);
    }
  }

  WiFi.scanDelete();
  Serial.println();
  delay(5000);
}
