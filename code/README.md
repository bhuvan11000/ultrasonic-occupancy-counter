# Production code — local room / queue counter

This folder contains the actual project code. The sketches in `firmware/` are
only hardware tests.

## Architecture

```text
HC-SR04 A/B
    │
    ▼
Arduino UNO
  - sensor debounce
  - room / queue state machine
  - count, capacity, totals
  - LEDs + buzzer
  - UART protocol
    │
    │ 9600 baud, newline-delimited
    ▼
ESP32 DevKit V1 30-pin
  - UART2: GPIO16 = RX, GPIO17 = TX
  - Wi-Fi
  - HTTP server
  - embedded dashboard
    │
    ▼
Browser on the same LAN
  http://<ESP32-IP>/
```

No MQTT and no cloud are used. The hosted dashboard can be added later by
changing the ESP32 network layer; the UNO protocol does not need to change.

## Folders

```text
code/
  uno/room_counter/          UNO production firmware
  esp32/local_dashboard/     ESP32 HTTP server firmware
    dashboard_html.h         generated from the dashboard source
    secrets.example.h        Wi-Fi template

dashboard/
  index.html                 dashboard source (separate folder)

tools/embed_dashboard.py     regenerates dashboard_html.h
firmware/                    hardware test sketches only
```

## 1. Configure Wi-Fi

Copy the example:

```bash
cp code/esp32/local_dashboard/secrets.example.h \
   code/esp32/local_dashboard/secrets.h
```

Edit `code/esp32/local_dashboard/secrets.h`:

```cpp
#define WIFI_SSID "your-2.4GHz-network"
#define WIFI_PASSWORD "your-password"
```

`secrets.h` is git-ignored. The ESP32 only supports **2.4 GHz Wi-Fi**.

If the configured network is not found within 12 seconds, the ESP32 starts its
own access point:

- SSID: `RoomCounter`
- Password: `counter1234`
- Dashboard: `http://192.168.4.1/`

## 2. Upload the UNO firmware

Open:

`code/uno/room_counter/room_counter.ino`

Select:

- Board: **Arduino Uno**
- Port: `/dev/ttyUSB0` (CH340 on this PC)

Upload it. The UNO runs independently even if the ESP32 is offline.

## 3. Upload the ESP32 firmware

Open:

`code/esp32/local_dashboard/local_dashboard.ino`

Select:

- Board: **DOIT ESP32 DEVKIT V1** (or **ESP32 Dev Module** if needed)
- Port: `/dev/ttyUSB1` (CP2102 on this PC)

Upload it. Open the ESP32 Serial Monitor at **115200 baud** to see the IP
address:

```text
Wi-Fi connected. Dashboard: http://192.168.1.42
mDNS: http://roomcounter.local/
```

Open that address in a browser on the same Wi-Fi network. If mDNS is supported,
`http://roomcounter.local/` also works.

## 4. Dashboard

The dashboard polls `/api/state` once per second. It shows:

- Current count, capacity, available spaces
- Room/queue mode
- Entry and exit totals and net flow
- Sensor A/B distance, active state, threshold marker and faults
- Last 20 events with relative age
- ESP32 IP, SSID, RSSI, uptime, free heap
- UNO link age and parse errors
- Last command and acknowledgement

Controls:

- Switch **Room / Queue**
- Set capacity
- Set detection threshold
- Correct the count to a specific value
- Reset the count
- Test the buzzer

## 5. Operating modes

### Room mode

- Sensor A = outside
- Sensor B = inside
- A then B: count +1
- B then A: count −1
- Both active at the start: rejected as ambiguous
- A crossing that times out: rejected

### Queue mode

- Sensor A = queue in
- Sensor B = queue out
- A valid A edge: count +1
- A valid B edge: count −1
- A B edge at zero: recorded as `INVALID`, count stays at zero

The count is kept in UNO RAM. It returns to zero after an UNO reset. Entry and
exit totals are also RAM-only.

## 6. Detection threshold

Default threshold: **80 cm**.

A sensor is considered triggered when its measured distance is at or below the
threshold for two consecutive samples. It clears after three consecutive
samples above `threshold + 12 cm`.

Use the dashboard’s **Detection threshold** control to match your doorway or
queue setup. The sensor cards show the raw distance and a dashed threshold
marker, so you can calibrate it while watching the beam.

## 7. UART protocol

UNO → ESP32:

```text
S,count,mode,capacity,in,out,distA,distB,threshold,flags,seq,millis
E,seq,type,delta,count,millis
A,verb,OK|ERR
```

ESP32 → UNO:

```text
C,MODE,ROOM
C,MODE,QUEUE
C,CAP,10
C,RESET
C,CORRECT,5
C,THRESH,80
C,BEEP
C,STATUS
```

`flags` bits:

| Bit | Meaning |
|---|---|
| 0 | Sensor A active |
| 1 | Sensor B active |
| 2 | Full |
| 3 | Sensor A fault/no echo |
| 4 | Sensor B fault/no echo |

`seq` is the room-mode sequence state:

| Value | State |
|---|---|
| 0 | Idle |
| 1 | A detected first |
| 2 | B detected first |
| 3 | Waiting for both sensors to clear |
| 4 | Ambiguous |

## 8. Dashboard source / regeneration

The dashboard is embedded in the ESP32 firmware as PROGMEM, so no LittleFS or
SPIFFS upload is required.

After editing `dashboard/index.html`, regenerate the header:

```bash
python3 tools/embed_dashboard.py
```

Then re-upload the ESP32 sketch.

## 9. Tests

Hardware tests remain in `firmware/`:

- `firmware/uno/01_outputs_test`
- `firmware/uno/02_ultrasonic_test`
- `firmware/uno/03_uart_test`
- `firmware/esp32/01_uart_test`
- `firmware/esp32/02_wifi_scan`

The production firmware assumes all of those tests have passed.

## 10. Troubleshooting

| Symptom | Check |
|---|---|
| Dashboard page does not open | ESP32 and browser on same Wi-Fi; use IP printed on Serial Monitor |
| Dashboard shows “UNO link lost” | UNO power, common GND, D3 divider, ESP32 GPIO16/17 |
| Count never changes | Sensor distances on dashboard; threshold; sensor A/B placement |
| Count changes in the wrong direction | Swap A/B interpretation or swap the sensor signal wires |
| Buzzer never sounds | Confirmed active-low module; D11; module VCC/GND |
| LEDs never light | D8/D9/D10 and series resistors |
| ESP32 resets when buzzer sounds | Check 5 V supply and common ground; the buzzer module draws current from 5 V, not the GPIO |
| Wi-Fi won’t connect | 2.4 GHz network, credentials, or use the `RoomCounter` fallback AP |

## 11. Later: hosted dashboard

The stable boundary is the UNO UART protocol. When moving to a hosted dashboard:

1. Keep the UNO firmware and UART format.
2. Change the ESP32 network layer to MQTT or HTTPS.
3. Move the dashboard HTML to the hosted frontend.
4. Add a database/backend for history and authentication.

The current dashboard is intentionally local-only and has no MQTT dependency.
