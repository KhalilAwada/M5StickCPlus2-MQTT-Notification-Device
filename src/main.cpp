/******************************************************************************
 *                             INCLUDE HEADERS
 ******************************************************************************/
#include <Arduino.h>
#include <ArduinoJson.h>
#include "SPIFFSManager.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <M5UnitLCD.h>
#include <M5Unified.h>
#include <PubSubClient.h>
// Removed <nlohmann/json.hpp>, <iostream>, and <sstream>

/******************************************************************************
 *                           CONFIGURATION MACROS
 ******************************************************************************/
#ifndef WIFI_SSID
#define WIFI_SSID "your_ssid"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "your_password"
#endif

#ifndef MQTT_HOST
#define MQTT_HOST "your_mqtt_host"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1886 // as integer
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME "mqtt_username"
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD "mqtt_password"
#endif

#ifndef MQTT_TOPIC
#define MQTT_TOPIC "mqtt_topic"
#endif

#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID "mqtt_client_id"
#endif

#ifndef MQTT_CLEAN_SESSION
#define MQTT_CLEAN_SESSION false
#endif

#ifndef MQTT_QOS
#define MQTT_QOS 1
#endif

#ifndef MQTT_RETAIN
#define MQTT_RETAIN false
#endif

#ifndef MQTT_TLS
#define MQTT_TLS true
#endif

#ifndef MQTT_TLS_INSECURE
#define MQTT_TLS_INSECURE false
#endif

#ifndef MQTT_TLS_CERT_REQS
#define MQTT_TLS_CERT_REQS ssl.CERT_REQUIRED
#endif

#ifndef MQTT_TLS_VERSION
#define MQTT_TLS_VERSION ssl.PROTOCOL_TLS
#endif

/******************************************************************************
 *                    GLOBAL OBJECTS & VARIABLES
 ******************************************************************************/
SPIFFSManager spiffsManager(SPIFFS);
WiFiClient wifiClient;
WiFiMulti wifiMulti;
PubSubClient mqttClient(wifiClient);
M5Canvas canvas(&M5.Display);
long mqttLastReconnectAttempt = 0;

/******************************************************************************
 *                        FUNCTION PROTOTYPES
 ******************************************************************************/
DynamicJsonDocument loadWifiConfig(SPIFFSManager &spiffsManager);
bool wifiConnect();
boolean mqttReconnect();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void displayWifiStatus();
DynamicJsonDocument updateWifiConfig(SPIFFSManager &spiffsManager, const char *ssid, const char *password);
void displayBatteryStatus();
void displayMQTTStatus();
void handleGithubEventJSON(const DynamicJsonDocument &event);
void handleGithubEvent(String message);

/******************************************************************************
 *                                SETUP
 ******************************************************************************/
void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  M5.begin(cfg);
  { // Configure speaker (using I2S custom settings)
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.buzzer = true;
    if (spk_cfg.use_dac || spk_cfg.buzzer) {
      spk_cfg.sample_rate = 192000; // Increase sample_rate for improved quality
    }
    M5.Speaker.config(spk_cfg);
  }
  M5.Power.begin();
  M5.Speaker.begin();

  delay(3000);
  Serial.println("Started");

  M5.setPrimaryDisplayType({m5::board_t::board_M5UnitLCD});
  M5.Display.setRotation(3);
  int textSize = M5.Display.height() / 68;
  if (textSize == 0) textSize = 1;
  M5.Display.setTextSize(textSize);

  /**************************************************************************
   *                Initialize the Scrollable Text Canvas
   **************************************************************************/
  canvas.setColorDepth(8); // Use 2-bit color depth to reduce memory usage
  canvas.createSprite(M5.Display.width(), M5.Display.height() - 25);
  canvas.setTextSize((float)canvas.width() / 200);
  canvas.setTextScroll(true);

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    canvas.println("SPIFFS Mount Failed");
    return;
  }

  DynamicJsonDocument wifiJSON = loadWifiConfig(spiffsManager);
  for (JsonObject network : wifiJSON.as<JsonArray>()) {
    const char* ssid = network["ssid"];
    const char* password = network["password"];
    Serial.printf("Network SSID: %s\n", ssid);
    wifiMulti.addAP(ssid, password);
  }

  mqttClient.setServer(MQTT_HOST, 1883);
  mqttClient.setCallback(mqttCallback);
}

/******************************************************************************
 *                         HELPER FUNCTIONS
 ******************************************************************************/
DynamicJsonDocument updateWifiConfig(SPIFFSManager &spiffsManager, const char *ssid, const char *password) {
  // Allocate 1024 bytes for the WiFi configuration document
  DynamicJsonDocument wifiDoc(1024);

  if (!spiffsManager.fileExists("/wifi.json")) {
    canvas.println("wifi.json does not exist, creating new file.");
    // Create an empty JSON array
    wifiDoc.to<JsonArray>();
  } else {
    String wifis = spiffsManager.readFile("/wifi.json");
    Serial.printf("Read wifi file content: '%s'\n", wifis.c_str());
    DeserializationError error = deserializeJson(wifiDoc, wifis);
    if (error || !wifiDoc.is<JsonArray>()) {
      canvas.println("Error parsing wifi.json; creating new array.");
      wifiDoc.clear();
      wifiDoc.to<JsonArray>();
    }
  }
  JsonArray array = wifiDoc.as<JsonArray>();

  bool found = false;
  for (JsonObject cred : array) {
    if (cred["ssid"] && strcmp(cred["ssid"], ssid) == 0) {
      cred["password"] = password;
      found = true;
      break;
    }
  }
  if (!found) {
    JsonObject newCred = array.createNestedObject();
    newCred["ssid"] = ssid;
    newCred["password"] = password;
  }

  String output;
  serializeJson(wifiDoc, output);
  spiffsManager.writeFile("/wifi.json", output.c_str());
  canvas.printf("Updated wifi config: %s\n", output.c_str());
  Serial.printf("Updated wifi config written: %s\n", output.c_str());
  canvas.pushSprite(0, 25);
  return wifiDoc;
}

DynamicJsonDocument loadWifiConfig(SPIFFSManager &spiffsManager) {
  DynamicJsonDocument wifiDoc(1024);
  wifiDoc = updateWifiConfig(spiffsManager, WIFI_SSID, WIFI_PASS);
  String wifis = spiffsManager.readFile("/wifi.json");
  Serial.printf("Read wifi file content: '%s'\n", wifis.c_str());
  DeserializationError error = deserializeJson(wifiDoc, wifis);
  if (error) {
    Serial.printf("JSON parsing error: %s\n", error.c_str());
    canvas.println("Error parsing wifi config");
  } else {
    canvas.printf("Loaded %d wifi networks\n", wifiDoc.as<JsonArray>().size());
    for (JsonObject network : wifiDoc.as<JsonArray>()) {
      Serial.printf("Loaded Network SSID: %s\n", network["ssid"].as<const char*>());
    }
  }
  canvas.pushSprite(0, 25);
  return wifiDoc;
}

void displayMQTTStatus() {
  int indicatorSize = 8;
  int batteryIconWidth = 24;
  int x = M5.Display.width() - batteryIconWidth - 25 - indicatorSize - 10;
  int y = 3;
  uint16_t color = mqttClient.connected() ? BLUE : DARKGREY;
  M5.Display.fillCircle(x + indicatorSize / 2, y + indicatorSize / 2, indicatorSize / 2, color);
}

void displayWifiStatus() {
  int iconSize = 20;
  int iconX = M5.Display.width() - iconSize;
  int iconY = 0;
  M5.Display.fillRect(iconX, iconY, iconSize, iconSize, BLACK);
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    int bars = (rssi >= -50) ? 4 : (rssi >= -60) ? 3 : (rssi >= -70) ? 2 : 1;
    for (int i = 0; i < 4; i++) {
      int barWidth = 2, spacing = 2;
      int x = iconX + spacing + i * (barWidth + spacing);
      int baseY = iconY + iconSize - spacing - 6;
      int barHeight = 2 * (i + 1);
      if (i < bars)
        M5.Display.fillRect(x, baseY - barHeight, barWidth, barHeight, GREEN);
      else
        M5.Display.drawRect(x, baseY - barHeight, barWidth, barHeight, WHITE);
    }
  } else {
    int padding = 4;
    int startX = iconX + padding, startY = iconY + padding;
    int endX = iconX + iconSize - padding, endY = iconY + iconSize - padding;
    M5.Display.drawLine(startX, startY, endX, endY, RED);
    M5.Display.drawLine(endX, startY, startX, endY, RED);
  }
}

void displayBatteryStatus() {
  int batteryIconWidth = 24, batteryIconHeight = 14;
  int batteryX = M5.Display.width() - batteryIconWidth - 25, batteryY = 3;
  M5.Display.fillRect(batteryX, batteryY, batteryIconWidth, batteryIconHeight, BLACK);
  std::int32_t batLevel = M5.Power.getBatteryLevel();
  int16_t batVoltage = M5.Power.getBatteryVoltage();
  int bodyWidth = batteryIconWidth - 4, bodyHeight = batteryIconHeight - 4;
  M5.Display.drawRect(batteryX, batteryY, bodyWidth, bodyHeight, DARKGREY);
  M5.Display.fillRect(batteryX + bodyWidth, batteryY + (bodyHeight / 2) - 2, 3, 4, DARKGREY);
  int fillWidth = ((bodyWidth - 2) * batLevel) / 100;
  M5.Display.fillRect(batteryX + 1, batteryY + 1, fillWidth, bodyHeight - 2, GREEN);
  if (batVoltage >= 4200) {
    int centerX = batteryX + bodyWidth / 2, centerY = batteryY + bodyHeight / 2;
    M5.Display.drawLine(centerX - 4, batteryY + 2, centerX, centerY, DARKGREEN);
    M5.Display.drawLine(centerX, centerY, centerX - 2, batteryY + bodyHeight - 2, DARKGREEN);
    M5.Display.drawLine(centerX - 3, batteryY + 2, centerX, centerY, DARKGREEN);
    M5.Display.drawLine(centerX + 1, centerY, centerX - 2, batteryY + bodyHeight - 2, DARKGREEN);
  }
}

bool wifiConnect() {
  bool connected = (wifiMulti.run() == WL_CONNECTED);
  if (connected)
    mqttLastReconnectAttempt = 0;
  else
    delay(1000);
  static unsigned long lastDisplayUpdate = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - lastDisplayUpdate >= 10000) {
    displayWifiStatus();
    displayBatteryStatus();
    displayMQTTStatus();
    lastDisplayUpdate = currentMillis;
  }
  return connected;
}

boolean mqttReconnect() {
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD, MQTT_TOPIC, 1, false, "", false)) {
    Serial.println("MQTT Connected");
    mqttClient.subscribe(MQTT_TOPIC);
  } else {
    Serial.println("MQTT Connection failed");
  }
  return mqttClient.connected();
}

/******************************************************************************
 *                          MQTT CALLBACK
 ******************************************************************************/
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // If the message is JSON
  if (message.startsWith("{") && message.endsWith("}")) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
    } else {
      const char* msgType = doc["msgType"];
      const char* msgGroup = doc["msgGroup"];
      if (strcmp(msgType, "event") == 0 && strcmp(msgGroup, "gh") == 0) {
        Serial.println("message supported");
        handleGithubEventJSON(doc);
      }
      else if (strcmp(msgType, "config") == 0 && strcmp(msgGroup, "wifi") == 0) {
        Serial.println("message supported");
        if (doc.containsKey("ssid") && doc.containsKey("password")) {
          updateWifiConfig(spiffsManager, doc["ssid"], doc["password"]);
          loadWifiConfig(spiffsManager);
        } else {
          Serial.println("Invalid wifi config message");
        }
      } else {
        Serial.println("message not supported");
      }
    }
  }
  // If message contains '|' use strtok() to split tokens
  else if (message.indexOf('|') != -1) {
    char buf[message.length() + 1];
    message.toCharArray(buf, sizeof(buf));
    const int maxTokens = 5;
    String tokens[maxTokens];
    int index = 0;
    char* token = strtok(buf, "|");
    while (token != NULL && index < maxTokens) {
      tokens[index++] = String(token);
      token = strtok(NULL, "|");
    }
    if (index == maxTokens && tokens[0] == "e" && tokens[1] == "gh") {
      handleGithubEvent(message);
    }
  }
  if (message == "clear") {
    canvas.clear();
  }
}

/******************************************************************************
 *                     HANDLE GITHUB EVENT (String)
 ******************************************************************************/
void handleGithubEvent(String message) {
  char buf[message.length() + 1];
  message.toCharArray(buf, sizeof(buf));
  const int maxTokens = 5;
  String tokens[maxTokens];
  int index = 0;
  char* token = strtok(buf, "|");
  while (token != NULL && index < maxTokens) {
    tokens[index++] = String(token);
    token = strtok(NULL, "|");
  }
  if (index < maxTokens) return;
  String _type = tokens[0];
  String _group = tokens[1];
  String _color = tokens[2];
  String _line = tokens[3];
  String _order = tokens[4];

  Serial.printf("Color: %s\n", _color.c_str());
  Serial.printf("Line: %s\n", _line.c_str());
  Serial.printf("Order: %s\n", _order.c_str());
  if (_color == "RED")
    canvas.setTextColor(RED);
  else if (_color == "GREEN")
    canvas.setTextColor(GREEN);
  else if (_color == "YELLOW")
    canvas.setTextColor(YELLOW);
  else if (_color == "CYAN")
    canvas.setTextColor(CYAN);
  else if (_color == "WHITE")
    canvas.setTextColor(WHITE);
  else if (_color == "BLACK")
    canvas.setTextColor(BLACK);
  else if (_color == "ORANGE")
    canvas.setTextColor(ORANGE);
  else if (_color == "DARKGREY")
    canvas.setTextColor(DARKGREY);
  else if (_color == "PURPLE")
    canvas.setTextColor(PURPLE);
  else
    canvas.setTextColor(WHITE);

  if (_order == "1") {
    if (_color == "RED") {
      M5.Speaker.tone(8000, 400);
      delay(250);
      M5.Speaker.tone(6000, 600);
    }
    else if (_color == "GREEN") {
      M5.Speaker.tone(8000, 100);
      delay(150);
      M5.Speaker.tone(10000, 100);
      delay(150);
      M5.Speaker.tone(12000, 200);
    }
    else {
      M5.Speaker.tone(5000, 150);
      delay(200);
      M5.Speaker.tone(5000, 150);
    }
  }
  canvas.printf("%s\n", _line.c_str());
  if (_order == "1") {
    int currentY = canvas.getCursorY();
    canvas.printf("---------------------------------\n");
  }
  canvas.pushSprite(0, 25);
}

/******************************************************************************
 *                 HANDLE GITHUB EVENT (JSON)
 ******************************************************************************/
void handleGithubEventJSON(const DynamicJsonDocument &event) {
  const char* eventType = event["type"];
  if (eventType) {
    if (strcmp(eventType, "workflow_run") == 0) {
      const char* status = event["status"];
      if (status) {
        if (strcmp(status, "queued") == 0)
          canvas.setTextColor(YELLOW);
        else if (strcmp(status, "in_progress") == 0)
          canvas.setTextColor(ORANGE);
        else if (strcmp(status, "completed") == 0) {
          const char* conclusion = event["conclusion"];
          if (conclusion) {
            if (strcmp(conclusion, "success") == 0)
              canvas.setTextColor(GREEN);
            else if (strcmp(conclusion, "cancelled") == 0)
              canvas.setTextColor(DARKGREY);
            else
              canvas.setTextColor(RED);
          }
        }
      }
    }
    else if (strcmp(eventType, "push") == 0)
      canvas.setTextColor(CYAN);
    else
      canvas.setTextColor(WHITE);
  }
  // if (event["lines"].is<JsonArray>()) {
  //   for (JsonVariant v : event["lines"].as<JsonArray>()) {
  //     canvas.printf("%s\n", v.as<const char*>());
  //   }
  // }
  canvas.setTextColor(WHITE);
}

/******************************************************************************
 *                                MAIN LOOP
 ******************************************************************************/
void loop() {
  if (wifiConnect()) {
    if (!mqttClient.connected()) {
      long now = millis();
      if (now - mqttLastReconnectAttempt > 5000) {
        mqttLastReconnectAttempt = now;
        if (mqttReconnect())
          mqttLastReconnectAttempt = 0;
        else {
          Serial.println("MQTT Connection failed");
          sleep(3000);
        }
      }
    }
    else {
      mqttClient.loop();
    }
  }
}