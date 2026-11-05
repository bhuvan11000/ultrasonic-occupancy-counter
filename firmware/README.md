# Hardware verification tests

All sketches in this folder compile successfully with:

- Arduino UNO: `arduino:avr:uno`
- ESP32 DevKit V1 30-pin: `esp32:esp32:esp32`

## Before uploading anything

> **If your LEDs are bare LEDs and do not have 220–330 Ω series resistors, disconnect D8, D9 and D10 now and skip Test 1.** Do not run the LED test with bare LEDs connected directly to the UNO.

1. **Disconnect USB power.**
2. Confirm every bare LED has a **220–330 Ω resistor in series**. Do not run the LED test without them.
3. Confirm the 3-pin buzzer module is wired:
   - `VCC` → 5 V rail
   - `GND` → GND rail
   - `I/O` → UNO D11
4. Confirm the UNO and ESP32 grounds are connected together.
5. Confirm no UNO 5 V wire goes directly to an ESP32 GPIO.
6. Confirm the UART divider is still:
   - UNO D3 → 1 kΩ → node → ESP32 GPIO16
   - node → 1 kΩ → 220 Ω → GND
7. Reconnect USB power.

The two USB adapters on this PC are:

- `/dev/ttyUSB0` — CH340, usually the Arduino UNO
- `/dev/ttyUSB1` — CP2102, usually the ESP32

Verify by unplugging one board at a time if necessary.

## Quick multimeter check before code

With USB disconnected:

- D3 to the divider node: about **1 kΩ**
- Divider node to GND: about **1.22 kΩ**
- UNO GND to ESP32 GND: continuity
- LED anodes/cathodes: no short directly across 5 V

With USB connected:

- Breadboard `+` rail to `−` rail: about **5 V**
- Each HC-SR04 VCC to GND: about **5 V**
- Do not measure the ESP32 GPIO16 node while the UNO is transmitting; it is a changing digital signal.

---

# Test 1 — UNO LEDs and buzzer

Sketch:

`firmware/uno/01_outputs_test/01_outputs_test.ino`

Upload to the UNO only. Open the UNO Serial Monitor at **115200 baud**.

Expected:

1. Green LED on D8 turns on, then off.
2. Red LED on D9 turns on, then off.
3. Blue LED on D10 turns on, then off.
4. All three turn on together, then off.
5. The buzzer produces one beep during either the `HIGH` or `LOW` test.

Record the buzzer result:

- Beep during `I/O = HIGH` → **active-high**
- Beep during `I/O = LOW` → **active-low**
- No beep at all → check VCC/GND, or the module may be passive instead of active

If one LED does not light:

- Check its resistor and orientation.
- Long leg = anode, short leg/flat side = cathode.
- Swap the LED only after disconnecting power.
- The UNO onboard D13 LED can confirm that the UNO itself is running.

---

# Test 2 — HC-SR04 sensors

Sketch:

`firmware/uno/02_ultrasonic_test/02_ultrasonic_test.ino`

Upload to the UNO only. Open the Serial Monitor at **115200 baud**.

Expected output:

```text
A: 24.3 cm   |   B: 61.8 cm
A: 23.9 cm   |   B: 60.9 cm
```

Move a flat object in front of each sensor. The value should change.

- `---` means no echo was received.
- Values near 0 or constantly `---` mean a wiring or power problem.
- The sensors are read alternately to reduce crosstalk.
- Keep the object at least 2 cm away; the HC-SR04 has a minimum range.

Check:

- Sensor A TRIG → D4, ECHO → D5
- Sensor B TRIG → D6, ECHO → D7
- Both sensors have 5 V and GND
- ECHO is connected only to the UNO, never directly to the ESP32

---

# Test 3 — UNO ↔ ESP32 UART

Sketches:

- UNO: `firmware/uno/03_uart_test/03_uart_test.ino`
- ESP32: `firmware/esp32/01_uart_test/01_uart_test.ino`

Upload the UNO sketch to the UNO and the ESP32 sketch to the ESP32.

Open **two separate Serial Monitors**, both at **115200 baud**:

- UNO monitor
- ESP32 monitor

Expected on the UNO monitor:

```text
UNO_TX seq=1 ms=1000
UNO_RX: ESP_TX seq=1 ms=1000
UNO_TX seq=2 ms=2000
UNO_RX: ESP_TX seq=2 ms=2000
```

Expected on the ESP32 monitor:

```text
ESP_TX seq=1 ms=1000
ESP_RX: UNO_TX seq=1 ms=1000
ESP_TX seq=2 ms=2000
ESP_RX: UNO_TX seq=2 ms=2000
```

This verifies both directions:

- UNO D3 → divider → ESP32 GPIO16
- ESP32 GPIO17 → UNO D2

If only one direction works:

- No UNO→ESP data: check the divider, ESP32 GPIO16, and common ground.
- No ESP→UNO data: check ESP32 GPIO17 → UNO D2 and common ground.
- Check that both UART links are at **9600 baud**, while both USB monitors are at **115200 baud**.
- Do not connect UNO 5 V directly to GPIO16 or GPIO17.

---

# Test 4 — ESP32 Wi-Fi

Sketch:

`firmware/esp32/02_wifi_scan/02_wifi_scan.ino`

Upload to the ESP32 only. Open the ESP32 Serial Monitor at **115200 baud**.

Expected:

```text
=== ESP32 WI-FI SCAN ===
Scanning...
Networks found: 7
1: MyWiFi  RSSI=-54 dBm  channel=6  encrypted
...
```

If no networks appear:

- Check that the board has the WROOM antenna connected/clear.
- Try again near a 2.4 GHz router.
- ESP32 does not scan 5 GHz-only networks.

---

# Test order

Run them in this order:

1. **Outputs** — LEDs and buzzer
2. **Ultrasonic** — both sensors
3. **UART** — UNO and ESP32 together
4. **Wi-Fi scan** — ESP32 networking

After all four pass, the hardware is verified. The next step is the actual room/queue counting state machine and the local dashboard server.
