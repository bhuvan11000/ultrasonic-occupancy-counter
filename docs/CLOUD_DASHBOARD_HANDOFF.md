# Hosted Dashboard — Architecture A Handoff

**Project:** Ultrasonic Occupancy Counter  
**Scope:** Hosted web dashboard for a single counter device, with historical events  
**Architecture:** MQTT for live state + HTTPS event ingestion + Turso (hosted SQLite)  
**Status:** Design handoff for the dashboard teammate

---

## 1. What is already done

The hardware side is complete and verified:

- Arduino UNO counts entries/exits and runs the room/queue state machine.
- ESP32 currently serves a **local-only** dashboard over LAN.
- The UNO ↔ ESP32 UART protocol is stable and must not change:

```text
S,count,mode,capacity,in,out,distA,distB,threshold,flags,seq,millis
E,seq,type,delta,count,millis
A,verb,OK|ERR
```

The hosted work is split:

| Owner | Responsibility |
|---|---|
| Hardware owner | ESP32 cloud gateway, MQTT publishing, HTTPS event POST, commands to UNO |
| Dashboard teammate | Next.js dashboard, Turso schema, MQTT-over-WebSockets client, auth, charts, deployment |
| Shared | Topic/payload contract, command/ack contract, test data, deployment checklist |

**Do not modify `code/uno/room_counter/`.** The UNO protocol is the stable boundary.

---

## 2. Architecture

Architecture A avoids a permanently running backend:

```text
                        ┌─────────────────────────────────────┐
                        │      EMQX Cloud Serverless          │
                        │  MQTT TLS :8883  |  WSS :8084 /mqtt │
                        └───────▲──────────────────┬──────────┘
          state/event/status/ack │                  │ live state/event/status/ack
          (device credentials)   │                  │ (read-only browser credentials)
                                 │                  ▼
┌─────────────┐   UART   ┌───────┴──────┐   ┌──────────────────────────────┐
│ Arduino UNO │◄────────►│ ESP32 cloud  │   │  Next.js dashboard on Vercel │
│  (counter)  │  9600    │   gateway    │   │  - MQTT.js over WSS          │
└─────────────┘          └───────┬──────┘   │  - commands via API route    │
                                 │          │  - charts / event table      │
                                 │ HTTPS    └──────────────▲───────────────┘
                                 │ event POST              │ SQL over HTTP
                                 ▼                         │
                        ┌──────────────────────────────────┴──────────┐
                        │              Turso (SQLite)                  │
                        │  occupancy_events, devices, command_audit    │
                        └─────────────────────────────────────────────┘
```

### Why this shape

- Live data is instant via MQTT retained state.
- Event history is durable in Turso.
- No serverless function holds a permanent MQTT connection.
- No always-on VM or container is needed.
- The dashboard can be built and tested with mock MQTT messages before the
  ESP32 cloud gateway is ready.

### Important rule

**Never insert every `state` message into Turso.** State is published every
500–1000 ms only for live display. Only discrete events (`IN`, `OUT`, `FULL`,
`INVALID`, `TIMEOUT`, `MODE`, `CAP`, `RESET`, `CORRECT`, `THRESH`) go into the
database.

---

## 3. Recommended stack

| Layer | Choice |
|---|---|
| Frontend | Next.js App Router + TypeScript |
| Styling | Tailwind CSS + shadcn/ui |
| Charts | Recharts |
| Browser live data | `mqtt` npm package over WSS |
| Hosting | Vercel |
| Database | Turso (libSQL/SQLite) |
| DB client | `@libsql/client` |
| Auth for controls | Simple admin password + signed HttpOnly cookie |
| MQTT broker | EMQX Cloud Serverless |

### Why EMQX Cloud Serverless

- MQTT over TLS on **8883** for the ESP32
- MQTT over secure WebSockets on **8084**, path `/mqtt`, for the browser
- Free serverless quota is enough for this project
- Per-client username/password authentication

If the team prefers HiveMQ Cloud, verify its WebSocket endpoint and ACL behavior
before committing. EMQX is the safer choice for this architecture.

---

## 4. MQTT contract

Device ID for the first unit: `room-01`.

| Topic | Direction | QoS | Retained | Purpose |
|---|---|---|---|---|
| `counter/room-01/status` | ESP32 → broker | 1 | **Yes** | Online/offline, Last Will |
| `counter/room-01/state` | ESP32 → broker | 0 | **Yes** | Live snapshot every 500–1000 ms |
| `counter/room-01/event` | ESP32 → broker | 1 | No | Live event notification |
| `counter/room-01/cmd` | Server → ESP32 | 1 | No | Commands from dashboard API |
| `counter/room-01/ack` | ESP32 → broker | 1 | No | Command acknowledgement |

### 4.1 Status

Online:

```json
{
  "deviceId": "room-01",
  "status": "online",
  "ts": 1762430400000
}
```

Last Will / offline:

```json
{
  "deviceId": "room-01",
  "status": "offline"
}
```

The offline payload is fixed at connection time. Do not put a dynamic timestamp
inside the Last Will payload.

### 4.2 State

Retained. The browser receives the latest state immediately after subscribing.

```json
{
  "deviceId": "room-01",
  "ts": 1762430400000,
  "unoOnline": true,
  "count": 7,
  "mode": "room",
  "capacity": 10,
  "in": 34,
  "out": 27,
  "distanceA": 42,
  "distanceB": 118,
  "threshold": 80,
  "flags": {
    "activeA": true,
    "activeB": false,
    "full": false,
    "faultA": false,
    "faultB": false
  },
  "sequence": 0,
  "esp": {
    "rssi": -58,
    "uptimeMs": 184200,
    "freeHeap": 214000,
    "ip": "192.168.1.42"
  }
}
```

### 4.3 Event

QoS 1. Also sent by HTTPS to `/api/ingest/event` for Turso persistence.

```json
{
  "eventId": "room-01-boot7f3a-61",
  "deviceId": "room-01",
  "seq": 61,
  "type": "IN",
  "delta": 1,
  "count": 7,
  "mode": "room",
  "capacity": 10,
  "ts": 1762430405000
}
```

`eventId` must be globally unique. The ESP32 should generate it as:

```text
<deviceId>-<bootId>-<unoEventSeq>
```

`bootId` is a short random/epoch value generated once per ESP32 boot, because the
UNO event sequence restarts at zero after an UNO reset.

Allowed event types:

`IN`, `OUT`, `FULL`, `INVALID`, `TIMEOUT`, `MODE`, `CAP`, `THRESH`, `RESET`,
`CORRECT`.

### 4.4 Command

Published only by the server-side API route, never directly by the browser.

```json
{
  "cmdId": "cmd-1762430410-01",
  "cmd": "capacity",
  "value": 15,
  "ts": 1762430410000
}
```

Supported commands:

| `cmd` | `value` |
|---|---|
| `mode` | `"room"` or `"queue"` |
| `capacity` | `1..99` |
| `threshold` | `10..300` |
| `correct` | `0..999` |
| `reset` | omitted |
| `beep` | omitted |
| `status` | omitted |

### 4.5 Acknowledgement

```json
{
  "cmdId": "cmd-1762430410-01",
  "verb": "CAP",
  "ok": true,
  "ts": 1762430410200
}
```

The dashboard should match `cmdId`, show a pending state, and time out after
about 5 seconds.

---

## 5. HTTPS event ingestion

The ESP32 sends every event to the Next.js API as well as publishing it over
MQTT.

### `POST /api/ingest/event`

Headers:

```http
Authorization: Bearer <INGEST_TOKEN>
Content-Type: application/json
```

Body: the event JSON from section 4.3.

Response:

- `201` on insert
- `200` if the `eventId` already exists
- `400` for invalid fields
- `401` for a bad token

Implementation requirement:

```sql
INSERT OR IGNORE INTO occupancy_events (...)
```

The API must be idempotent because MQTT QoS 1 and HTTPS retries can deliver the
same event more than once.

The ESP32 should retry a failed POST up to 3 times with a short backoff and keep
a small in-memory event queue. It should not block the UART/sensor loop.

---

## 6. Turso schema

Use UTC ISO-8601 strings or Unix milliseconds consistently. Recommended: store
`occurred_at` and `received_at` as UTC ISO-8601 text, and expose epoch
milliseconds in APIs if the frontend prefers numbers.

```sql
CREATE TABLE devices (
  device_id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  location TEXT,
  capacity INTEGER NOT NULL DEFAULT 10,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  last_seen_at TEXT
);

CREATE TABLE occupancy_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  event_id TEXT NOT NULL UNIQUE,
  device_id TEXT NOT NULL,
  seq INTEGER NOT NULL,
  type TEXT NOT NULL,
  delta INTEGER NOT NULL DEFAULT 0,
  count_after INTEGER NOT NULL,
  mode TEXT NOT NULL,
  capacity INTEGER NOT NULL,
  occurred_at TEXT NOT NULL,
  received_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX idx_events_device_time
  ON occupancy_events(device_id, occurred_at);

CREATE TABLE command_audit (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  cmd_id TEXT NOT NULL UNIQUE,
  device_id TEXT NOT NULL,
  cmd TEXT NOT NULL,
  value TEXT,
  requested_by TEXT,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  ack_ok INTEGER,
  ack_at TEXT
);
```

Seed the first device:

```sql
INSERT INTO devices (device_id, name, location, capacity)
VALUES ('room-01', 'Main Room', 'Entrance', 10);
```

### What to derive from events

- Occupancy / queue-length timeline: use `count_after` over time.
- IN/OUT totals: sum `delta` by type.
- Hourly footfall: group `IN`/`OUT` by hour.
- Peak occupancy: max `count_after`.
- Time at capacity: duration where count was at capacity.
- Invalid/timeout rate: count of `INVALID` + `TIMEOUT` versus valid crossings.
- Estimated queue wait in queue mode: `current_count / average OUT per minute`.

Do **not** add a `state_snapshots` table unless there is a demonstrated need.
Live state belongs in MQTT retained messages.

---

## 7. API routes

Use the Next.js **Node.js runtime** for routes that use `mqtt` or
`@libsql/client`:

```ts
export const runtime = "nodejs";
```

### Public read-only routes

| Route | Purpose |
|---|---|
| `GET /api/events?deviceId=room-01&limit=200` | Recent events, newest first |
| `GET /api/events?deviceId=room-01&from=...&to=...` | Historical range |
| `GET /api/summary?deviceId=room-01&range=24h` | Aggregated chart data |
| `GET /api/health` | Deployment health |

### Protected routes

| Route | Purpose |
|---|---|
| `POST /api/login` | Check admin password, set signed HttpOnly cookie |
| `POST /api/logout` | Clear cookie |
| `POST /api/command` | Publish a command to `counter/{deviceId}/cmd` |
| `POST /api/ingest/event` | ESP32 event ingestion, Bearer token |

### `POST /api/command`

Request:

```json
{
  "deviceId": "room-01",
  "cmd": "capacity",
  "value": 15
}
```

Server behavior:

1. Verify the admin session cookie.
2. Validate `cmd` and `value`.
3. Generate `cmdId`.
4. Connect to MQTT with server-side write credentials.
5. Publish JSON to `counter/room-01/cmd`, QoS 1.
6. Disconnect.
7. Insert a row into `command_audit`.
8. Return `202` with `{ "cmdId": "..." }`.

The dashboard should not expose MQTT write credentials.

### `POST /api/ingest/event`

Validate:

- `eventId` is non-empty and unique
- `deviceId` matches an existing device
- `type` is in the allowed list
- `count` and `delta` are sane integers
- `ts` is within a reasonable clock-skew window

Use `INSERT OR IGNORE` and return `200` for duplicates.

---

## 8. Authentication and security

### Recommended for this project

- **Public read-only dashboard** (occupancy data is not sensitive).
- **Controls protected by a shared admin password**:
  - `ADMIN_PASSWORD` in Vercel env.
  - `/api/login` sets a signed HttpOnly cookie.
  - `/api/command` requires the cookie.
  - Use `jose` or Node `crypto` HMAC; do not store the password in the client.
- No full user management unless the team wants it.

If the project needs named users or GitHub login, use Auth.js with GitHub OAuth,
but that is optional.

### MQTT credentials

Create three separate MQTT users:

| User | Permissions |
|---|---|
| `device-room-01` | Publish `counter/room-01/state`, `event`, `status`, `ack`; subscribe `counter/room-01/cmd` |
| `web-readonly` | Subscribe `counter/room-01/state`, `status`, `event`, `ack`; no publish |
| `server-write` | Publish `counter/room-01/cmd`; no broad subscribe |

The browser uses only `web-readonly`. The server uses `server-write`.

`NEXT_PUBLIC_MQTT_*` values are visible to the browser. That is acceptable only
because the web user is read-only and restricted to this device's topics.

### Secrets

- Never commit `.env.local`.
- Rotate `INGEST_TOKEN`, `ADMIN_PASSWORD`, MQTT passwords, and Turso tokens if
  they are exposed.
- Keep the ESP32 ingest token out of the dashboard client.

---

## 9. Environment variables

Create `.env.local`:

```bash
# Turso
TURSO_DATABASE_URL=libsql://...
TURSO_AUTH_TOKEN=...

# MQTT server-side (commands)
MQTT_URL=mqtts://<host>:8883
MQTT_WRITE_USERNAME=server-write
MQTT_WRITE_PASSWORD=...

# MQTT browser-side (read-only)
NEXT_PUBLIC_MQTT_URL=wss://<host>:8084/mqtt
NEXT_PUBLIC_MQTT_USERNAME=web-readonly
NEXT_PUBLIC_MQTT_PASSWORD=...

# Device ingestion
INGEST_TOKEN=...

# Admin controls
ADMIN_PASSWORD=...
SESSION_SECRET=...

# Optional
NEXT_PUBLIC_DEVICE_ID=room-01
```

Set the same values in Vercel Project Settings → Environment Variables.

---

## 10. Frontend requirements

### Live view

- Current count, capacity, available spaces
- Room/queue mode
- Connection status: MQTT connected, device online, UNO online
- Last update age
- Sensor A/B distance, active state, threshold marker, fault state
- Entry/exit totals, net flow
- Last event

### History and charts

- Occupancy / queue-length timeline
- IN vs OUT per hour or day
- Peak occupancy
- Time at capacity
- Invalid/timeout ratio
- Event table with filters and pagination
- CSV export

### Controls

- Room / Queue
- Capacity
- Detection threshold
- Correct count
- Reset count
- Test buzzer

Controls must show pending state until `ack` arrives, and must handle timeout.

### UX requirements

- Mobile responsive
- Clear offline/stale states
- Keyboard focus visible
- No secrets in client JavaScript beyond read-only MQTT credentials
- UTC in the database; local time in the UI
- Do not show a blank page while MQTT is connecting

---

## 11. Local development without hardware

The dashboard teammate can build everything before the ESP32 cloud gateway is
ready.

Use **MQTTX** or a small MQTT.js publisher to publish retained state and events
to the broker with the device user, or use a local broker.

Suggested test sequence:

1. Publish retained `status = online`.
2. Publish retained `state` with count 7.
3. Publish an `IN` event.
4. POST the same event to `/api/ingest/event` and confirm it appears in Turso.
5. POST it again and confirm the unique constraint prevents a duplicate.
6. Publish a command from `/api/command` and verify the ESP32/mock subscriber
   receives it.
7. Publish an `ack` and verify the dashboard clears the pending state.

For local-only testing, `docker run` a Mosquitto broker with WebSockets enabled,
or use the public EMQX test broker only for throwaway data. Do not put real
credentials or occupancy data on a public broker.

---

## 12. Deployment checklist

### Turso

- [ ] Create database
- [ ] Apply schema
- [ ] Seed `room-01`
- [ ] Create auth token
- [ ] Add `TURSO_DATABASE_URL` and `TURSO_AUTH_TOKEN` to Vercel

### EMQX Cloud

- [ ] Create serverless deployment
- [ ] Enable TLS (8883) and WSS (8084)
- [ ] Create device, web-readonly, and server-write users
- [ ] Apply topic ACLs
- [ ] Add credentials to Vercel and ESP32 config

### Vercel

- [ ] Import GitHub repository
- [ ] Set all environment variables
- [ ] Confirm API routes use Node.js runtime
- [ ] Deploy from `main`
- [ ] Test `/api/health`
- [ ] Test MQTT WSS connection from the deployed domain

### ESP32

- [ ] Add cloud gateway sketch
- [ ] Configure Wi-Fi, MQTT host, device credentials, ingest URL/token
- [ ] Sync time with NTP for TLS and event timestamps
- [ ] Verify retained state, events, commands, acknowledgements, LWT
- [ ] Confirm local dashboard fallback still works

---

## 13. Acceptance criteria

The hosted dashboard is done when:

- [ ] It loads on a phone and desktop.
- [ ] It shows live state within 2 seconds of opening.
- [ ] It shows `offline` when the ESP32 disconnects via LWT.
- [ ] It shows `UNO link lost` when the UNO stops sending UART state.
- [ ] It displays the last 200 events from Turso.
- [ ] It renders occupancy history and IN/OUT charts from Turso.
- [ ] A command from the dashboard reaches the ESP32 and receives an ack.
- [ ] Commands cannot be sent without admin login.
- [ ] Duplicate event POSTs do not create duplicate rows.
- [ ] Refreshing the page preserves history.
- [ ] No write credentials or admin password are exposed in client code.
- [ ] The local ESP32 dashboard remains functional as a fallback.

---

## 14. Failure handling

| Failure | Expected behavior |
|---|---|
| MQTT broker unreachable | Dashboard shows live data disconnected; history still loads |
| ESP32 offline | LWT sets status offline; dashboard marks device offline |
| UNO offline | ESP32 publishes `unoOnline: false`; dashboard shows UNO link lost |
| HTTPS ingest fails | ESP32 retries; MQTT event still shows live |
| Duplicate event | Turso unique constraint ignores duplicate |
| Command timeout | Dashboard shows failure after ~5 s |
| Turso unavailable | History endpoints return error; live MQTT continues |
| Wi-Fi credentials wrong | ESP32 cloud gateway reconnects with backoff; local fallback remains |

---

## 15. Out of scope

- Multi-tenant user management
- Billing
- Mobile native app
- Camera/computer-vision counting
- Cloud-side ML
- Storing every raw sensor sample
- Controlling the device outside the defined command set

---

## 16. Open decisions

These are the only choices that still need confirmation:

1. **Auth:** shared admin password (recommended) or GitHub OAuth via Auth.js?
2. **Broker:** EMQX Cloud Serverless (recommended) or HiveMQ Cloud?
3. **Framework:** Next.js App Router (recommended) or React + Vite?
4. **Device count:** one `room-01` now, but schema supports multiple. Confirm.
5. **Data retention:** keep events indefinitely or prune after 90 days?
6. **Public read access:** public read-only dashboard, or require login for viewing too?

Until confirmed, use the recommended defaults above.

---

## 17. Handoff summary for teammate

Build the Next.js app with:

- MQTT.js WSS live subscription using `web-readonly` credentials
- Turso event history via `@libsql/client`
- `/api/ingest/event` with Bearer token and idempotent insert
- `/api/command` with admin cookie and server-side MQTT publish
- `/api/events` and `/api/summary` for charts
- Recharts timeline and IN/OUT charts
- Responsive live dashboard and event table
- Vercel deployment with environment variables

The hardware owner will provide:

- ESP32 cloud gateway that publishes the topics above
- Device MQTT credentials
- Ingest URL/token configuration
- A mock event stream for early UI development

The UNO firmware and UART protocol will not change.
