# M5StickC MQTT Notification Device

A low-power notification device built on the M5StickC platform that receives MQTT notifications, displays scrolling text, and provides both visual and audio alerts. This project is designed for applications where notifications (such as GitHub events) are received infrequently, allowing the device to conserve power by dimming its display and selectively waking up when needed.

---

## Features

- **MQTT Connectivity:**  
  Connects to an MQTT broker to receive notifications in JSON or delimited string format.
  
- **Scrolling Display:**  
  Uses an M5Canvas to display notification text that scrolls automatically.

- **Status Indicators:**  
  Displays visual indicators for WiFi signal strength, battery status, and MQTT connection status.

- **Audio Alerts:**  
  Plays multi-tone audio sequences (success, failure, or generic alerts) based on the notification content.

- **WiFi Configuration via SPIFFS:**  
  Reads and updates WiFi configuration stored on SPIFFS. Remote configuration is supported via MQTT messages.

- **Power Optimization:**  
  Implements a display dimming and gradual fade-out strategy. The device remains in low-power mode until a button press (BtnB) increases brightness for notifications.

---

## Hardware Requirements

- **M5StickC** (or M5StickC Plus)
- USB cable and power supply
- A working WiFi network
- An MQTT broker (local or cloud-based)

---

## Software Requirements

- [PlatformIO](https://platformio.org/) (or Arduino IDE with ESP32 support)
- M5Stack Libraries (M5UnitLCD, M5Unified, etc.)
- [ArduinoJson](https://arduinojson.org/)
- [PubSubClient](https://pubsubclient.knolleary.net/)
- SPIFFS Manager

---

## Installation

1. **Clone the Repository:**

   ```bash
   git clone https://github.com/KhalilAwada/M5StickCPlus2-MQTT-Notification-Device.git
   cd M5StickCPlus2-MQTT-Notification-Device
   ```

---

## MQTT Message Formats

The device subscribes to a single MQTT topic (configured via `MQTT_TOPIC`) and
accepts three payload styles: **JSON**, **pipe-delimited**, and **plain text**.
Publish at **QoS ≥ 1** if you want messages queued while the device is offline
(the device subscribes with `cleanSession=false` and QoS 1).

### 1. WiFi config (JSON)

Updates the credentials stored in `/wifi.json` on LittleFS and reconnects.

```json
{
  "msgType": "config",
  "msgGroup": "wifi",
  "ssid": "YourHomeSSID",
  "password": "YourHomePassword"
}
```

Compact form on a single line also works:

```json
{"msgType":"config","msgGroup":"wifi","ssid":"YourMobileHotspot","password":"YourHotspotPassword"}
```

### 2. GitHub event (JSON)

Renders one or more colored lines with a status glyph. Color/glyph is derived
from `type`, `status`, and `conclusion`.

Accepted key names (both legacy and current producer format work):

| Field          | Legacy        | Current        |
| -------------- | ------------- | -------------- |
| Message type   | `msgType`     | `messageType`  |
| Message group  | `msgGroup`    | `messageGroup` |
| Group value    | `gh`          | `github`       |
| Body text      | `message` (string) | `lines` (array of strings) |

Glyph / color matrix:

| `type`         | `status`      | `conclusion` | Color     | Glyph |
| -------------- | ------------- | ------------ | --------- | ----- |
| `workflow_run` | `queued`      | —            | YELLOW    | `... `|
| `workflow_run` | `in_progress` | —            | ORANGE    | `>> ` |
| `workflow_run` | `completed`   | `success`    | GREEN     | `OK ` |
| `workflow_run` | `completed`   | `cancelled`  | DARKGREY  | `-- ` |
| `workflow_run` | `completed`   | other        | RED       | `X  ` |
| `push`         | —             | —            | CYAN      | `+ `  |

Current producer format (text in `lines` array):

```json
{
  "messageType": "event",
  "messageGroup": "github",
  "type": "workflow_run",
  "status": "in_progress",
  "conclusion": "",
  "id": 24968663440,
  "organization": "KarmatechConsulting",
  "repository": "nextjs-test",
  "lines": [
    "KarmatechConsulting / nextjs-test - workflow_run -",
    "Build and Publish Docker Image - (in_progress) - [24968663440]",
    "by KhalilAwada - 4/26/2026, 10:28:53 PM"
  ]
}
```

Legacy compact form (text in `message`):

```json
{
  "msgType": "event",
  "msgGroup": "gh",
  "type": "workflow_run",
  "status": "completed",
  "conclusion": "success",
  "message": "build #421 passed"
}
```

### 3. Grafana event (JSON)

`messageType=event`, `messageGroup=grafana`. Renders the `lines` array using
caller-supplied colors. Both `color` (foreground) and `bgColor` (text
background) accept hex strings in `0xRRGGBB` or `#RRGGBB` form. The display
is 8-bit color depth so values are rounded to the nearest representable
shade. A small status glyph is prefixed when `status` is recognized
(`firing`/`alerting` → `!! `, `resolved` → `OK `).

```json
{
  "messageType": "event",
  "messageGroup": "grafana",
  "status": "firing",
  "color": "0x000000",
  "bgColor": "0xff9966",
  "lines": ["text goes here"]
}
```

### 4. Generic JSON fallback

Any other JSON payload is surfaced on screen. The device tries common keys
(`message`, `text`, `title`, `body`) first, otherwise it prints the compact
JSON (truncated to 200 chars).

```json
{ "message": "Backup completed" }
```

```json
{ "title": "Door sensor", "body": "Front door opened" }
```

### 5. Pipe-delimited GitHub event (legacy)

Format: `e|gh|<color>|<line>|<order>` where `<order>=1` plays a tone and
appends a separator.

```text
e|gh|green|build #421 passed|1
e|gh|red|tests failed on main|1
e|gh|cyan|new commit pushed|0
```

Recognized colors: `red`, `green`, `yellow`, `orange`, `cyan`, `white`.

### 6. Control commands (plain text)

| Payload  | Effect                          |
| -------- | ------------------------------- |
| `clear`  | Clears the screen.              |
| anything | Printed verbatim in white text. |

```text
clear
```

```text
Coffee is ready
```