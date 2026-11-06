/*
  Room / Queue Capacity Counter — ESP32 local dashboard

  The ESP32 only handles networking:
    - UART2 receives state/events from the UNO
    - UART2 sends commands to the UNO
    - HTTP server serves the local dashboard
    - No MQTT, no cloud

  UART2:
    GPIO16 = RX  <- UNO D3 through the 5V -> 3.3V divider
    GPIO17 = TX  -> UNO D2
    GND common with the UNO

  Dashboard:
    http://<ESP32-IP>/
    or http://roomcounter.local/ when mDNS works
    If Wi-Fi fails, the ESP32 starts AP "RoomCounter" / "counter1234"
    and the dashboard is at http://192.168.4.1/
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "secrets.h"
#include "dashboard_html.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
const uint8_t UNO_RX_PIN = 16;
const uint8_t UNO_TX_PIN = 17;
const unsigned long UNO_LINK_TIMEOUT_MS = 3000;
const uint8_t EVENT_CAPACITY = 20;

const char *AP_SSID = "RoomCounter";
const char *AP_PASSWORD = "counter1234";

WebServer server(80);

// ---------------------------------------------------------------------------
// State received from the UNO
// ---------------------------------------------------------------------------
struct DeviceState {
  int count = 0;
  int mode = 0;  // 0 = room, 1 = queue
  int capacity = 10;
  int in = 0;
  int out = 0;
  int distanceA = -1;
  int distanceB = -1;
  int threshold = 80;
  uint8_t flags = 0;
  uint8_t sequence = 0;
  unsigned long unoMillis = 0;
};

struct Event {
  unsigned long sequence = 0;
  char type[14] = "";
  int delta = 0;
  int count = 0;
  unsigned long receivedMs = 0;
};

DeviceState device;
Event events[EVENT_CAPACITY];
uint8_t eventHead = 0;
uint8_t eventCount = 0;

unsigned long lastStateMs = 0;
uint32_t parseErrors = 0;
String lastCommand;
String lastAck;

bool apMode = false;
unsigned long lastReconnectMs = 0;

char uartLine[160];
uint8_t uartLineLength = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

bool unoOnline() {
  return lastStateMs != 0 && (millis() - lastStateMs) < UNO_LINK_TIMEOUT_MS;
}

void addEvent(unsigned long sequence, const char *type, int delta, int count) {
  Event &event = events[eventHead];
  event.sequence = sequence;
  strncpy(event.type, type, sizeof(event.type) - 1);
  event.type[sizeof(event.type) - 1] = '\0';
  event.delta = delta;
  event.count = count;
  event.receivedMs = millis();

  eventHead = (eventHead + 1) % EVENT_CAPACITY;
  if (eventCount < EVENT_CAPACITY) eventCount++;
}

void sendUno(const String &line) {
  Serial2.println(line);
  lastCommand = line;
  Serial.print(F("UART TX: "));
  Serial.println(line);
}

// ---------------------------------------------------------------------------
// UART parser
// ---------------------------------------------------------------------------
void parseUnoLine(const char *line) {
  if (line[0] == 'S' && line[1] == ',') {
    int count, mode, capacity, in, out, distanceA, distanceB, threshold, flags, sequence;
    unsigned long unoMillis;

    int parsed = sscanf(line,
                        "S,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu",
                        &count, &mode, &capacity, &in, &out,
                        &distanceA, &distanceB, &threshold,
                        &flags, &sequence, &unoMillis);

    if (parsed == 11) {
      device.count = count;
      device.mode = mode;
      device.capacity = capacity;
      device.in = in;
      device.out = out;
      device.distanceA = distanceA;
      device.distanceB = distanceB;
      device.threshold = threshold;
      device.flags = (uint8_t)flags;
      device.sequence = (uint8_t)sequence;
      device.unoMillis = unoMillis;
      lastStateMs = millis();
    } else {
      parseErrors++;
    }
    return;
  }

  if (line[0] == 'E' && line[1] == ',') {
    unsigned long sequence, unoMillis;
    char type[16];
    int delta, count;

    int parsed = sscanf(line, "E,%lu,%15[^,],%d,%d,%lu",
                        &sequence, type, &delta, &count, &unoMillis);

    if (parsed == 5) {
      addEvent(sequence, type, delta, count);
    } else {
      parseErrors++;
    }
    return;
  }

  if (line[0] == 'A' && line[1] == ',') {
    char verb[16], status[8];
    int parsed = sscanf(line, "A,%15[^,],%7s", verb, status);
    if (parsed == 2) {
      lastAck = String(verb) + "," + status;
    } else {
      parseErrors++;
    }
    return;
  }

  if (line[0] != '\0') {
    parseErrors++;
  }
}

void pollUno() {
  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();

    if (c == '\n') {
      uartLine[uartLineLength] = '\0';
      if (uartLineLength > 0) parseUnoLine(uartLine);
      uartLineLength = 0;
    } else if (c != '\r') {
      if (uartLineLength < sizeof(uartLine) - 1) {
        uartLine[uartLineLength++] = c;
      } else {
        uartLineLength = 0;
        parseErrors++;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// JSON state
// ---------------------------------------------------------------------------
String buildStateJson() {
  String json;
  json.reserve(4096);

  String ip;
  String ssid;
  int rssi = 0;
  String wifiMode;

  if (WiFi.getMode() == WIFI_AP) {
    ip = WiFi.softAPIP().toString();
    ssid = WiFi.softAPSSID();
    wifiMode = "ap";
  } else {
    ip = WiFi.localIP().toString();
    ssid = WiFi.SSID();
    rssi = WiFi.RSSI();
    wifiMode = "sta";
  }

  long stateAge = lastStateMs == 0 ? -1 : (long)(millis() - lastStateMs);

  json += F("{\"device\":{");
  json += F("\"count\":"); json += String(device.count);
  json += F(",\"mode\":\""); json += device.mode == 1 ? F("queue") : F("room"); json += '"';
  json += F(",\"capacity\":"); json += String(device.capacity);
  json += F(",\"in\":"); json += String(device.in);
  json += F(",\"out\":"); json += String(device.out);
  json += F(",\"distanceA\":"); json += String(device.distanceA);
  json += F(",\"distanceB\":"); json += String(device.distanceB);
  json += F(",\"threshold\":"); json += String(device.threshold);
  json += F(",\"flags\":"); json += String(device.flags);
  json += F(",\"sequence\":"); json += String(device.sequence);
  json += F(",\"unoUptimeMs\":"); json += String(device.unoMillis);
  json += F(",\"online\":"); json += unoOnline() ? F("true") : F("false");
  json += F("},\"esp\":{");
  json += F("\"ip\":\""); json += jsonEscape(ip); json += '"';
  json += F(",\"ssid\":\""); json += jsonEscape(ssid); json += '"';
  json += F(",\"wifiMode\":\""); json += wifiMode; json += '"';
  json += F(",\"rssi\":"); json += String(rssi);
  json += F(",\"uptimeMs\":"); json += String(millis());
  json += F(",\"freeHeap\":"); json += String(ESP.getFreeHeap());
  json += F(",\"lastStateAgeMs\":"); json += String(stateAge);
  json += F(",\"parseErrors\":"); json += String(parseErrors);
  json += F(",\"lastCommand\":\""); json += jsonEscape(lastCommand); json += '"';
  json += F(",\"lastAck\":\""); json += jsonEscape(lastAck); json += '"';
  json += F("},\"events\":[");

  for (uint8_t i = 0; i < eventCount; i++) {
    uint8_t index = (eventHead + EVENT_CAPACITY - 1 - i) % EVENT_CAPACITY;
    Event &event = events[index];

    if (i > 0) json += ',';
    json += F("{\"seq\":"); json += String(event.sequence);
    json += F(",\"type\":\""); json += jsonEscape(String(event.type)); json += '"';
    json += F(",\"delta\":"); json += String(event.delta);
    json += F(",\"count\":"); json += String(event.count);
    json += F(",\"ageMs\":"); json += String(millis() - event.receivedMs);
    json += '}';
  }

  json += F("]}");
  return json;
}

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------
void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleState() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buildStateJson());
}

void handleCommand() {
  if (!server.hasArg("cmd")) {
    server.send(400, "application/json", F("{\"ok\":false,\"error\":\"Missing cmd\"}"));
    return;
  }

  String cmd = server.arg("cmd");
  String value = server.arg("value");
  cmd.toLowerCase();
  value.trim();

  String line;

  if (cmd == "mode") {
    value.toLowerCase();
    if (value == "room") line = F("C,MODE,ROOM");
    else if (value == "queue") line = F("C,MODE,QUEUE");
  } else if (cmd == "capacity") {
    int capacity = value.toInt();
    if (capacity >= 1 && capacity <= 99) line = "C,CAP," + String(capacity);
  } else if (cmd == "threshold") {
    int threshold = value.toInt();
    if (threshold >= 10 && threshold <= 300) line = "C,THRESH," + String(threshold);
  } else if (cmd == "correct") {
    int count = value.toInt();
    if (count >= 0 && count <= 999) line = "C,CORRECT," + String(count);
  } else if (cmd == "reset") {
    line = F("C,RESET");
  } else if (cmd == "beep") {
    line = F("C,BEEP");
  } else if (cmd == "status") {
    line = F("C,STATUS");
  }

  if (line.length() == 0) {
    server.send(400, "application/json", F("{\"ok\":false,\"error\":\"Unknown or invalid command\"}"));
    return;
  }

  sendUno(line);
  server.send(200, "application/json", F("{\"ok\":true}"));
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
bool connectWiFi() {
  Serial.println(F("Connecting to Wi-Fi..."));
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    Serial.print(F("Wi-Fi connected. Dashboard: http://"));
    Serial.println(WiFi.localIP());

    if (MDNS.begin("roomcounter")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println(F("mDNS: http://roomcounter.local/"));
    }
    return true;
  }

  Serial.println(F("Wi-Fi failed. Starting access point."));
  WiFi.mode(WIFI_AP);
  apMode = WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print(F("AP: "));
  Serial.print(AP_SSID);
  Serial.print(F(" / "));
  Serial.println(AP_PASSWORD);
  Serial.print(F("Dashboard: http://"));
  Serial.println(WiFi.softAPIP());
  return false;
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, UNO_RX_PIN, UNO_TX_PIN);
  delay(300);

  Serial.println();
  Serial.println(F("=== Room / Queue local dashboard ==="));

  connectWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/command", HTTP_POST, handleCommand);
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println(F("HTTP server started."));
  delay(500);
  sendUno("C,STATUS");
}

void loop() {
  server.handleClient();
  pollUno();

  if (!apMode && WiFi.status() != WL_CONNECTED && millis() - lastReconnectMs > 10000) {
    lastReconnectMs = millis();
    Serial.println(F("Wi-Fi lost, reconnecting..."));
    WiFi.reconnect();
  }
}
