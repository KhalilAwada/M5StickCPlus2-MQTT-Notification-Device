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
   git clone https://github.com/yourusername/m5stickc-mqtt-notification.git
   cd m5stickc-mqtt-notification