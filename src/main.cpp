/******************************************************************************
 *                             INCLUDE HEADERS
 ******************************************************************************/
#include <Arduino.h>
#include "SPIFFSManager.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <M5UnitLCD.h>
#include <M5Unified.h>
#include <nlohmann/json.hpp>
#include <PubSubClient.h>

// #include "secrets.h" // Uncomment if using secret credentials

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
 *                         GLOBAL OBJECTS & VARIABLES
 ******************************************************************************/
SPIFFSManager spiffsManager(SPIFFS);
WiFiClient wifiClient;
WiFiMulti wifiMulti;           // Manages multiple WiFi networks
PubSubClient mqttClient(wifiClient); // MQTT client using WiFi

long mqttLastReconnectAttempt = 0;

/******************************************************************************
 *                        FUNCTION PROTOTYPES
 ******************************************************************************/
nlohmann::json loadWifiConfig(SPIFFSManager& spiffsManager);
bool wifiConnect();
boolean mqttReconnect();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void displayWifiStatus();
void displayBatteryStatus();

/******************************************************************************
 *                                SETUP
 ******************************************************************************/
void setup() {
  Serial.begin(115200);

  // Initialize M5Stack and power management
  M5.begin();
  M5.Power.begin();

  delay(3000);
  Serial.println("Started");

  // Configure primary display type and orientation
  M5.setPrimaryDisplayType({m5::board_t::board_M5UnitLCD});
  M5.Display.setRotation(3); // 90° clockwise rotation

  // Set appropriate text size based on display height
  int textSize = M5.Display.height() / 68;
  if (textSize == 0) {
    textSize = 1;
  }
  M5.Display.setTextSize(textSize);

  // Initialize SPIFFS
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    M5.Display.println("SPIFFS Mount Failed");
    return;
  }

  // Load WiFi configuration from file
  nlohmann::json wifiJSON = loadWifiConfig(spiffsManager);

  // Add each network from the configuration file
  for (const auto& network : wifiJSON) {
    String ssid = network["ssid"].get<std::string>().c_str();
    String password = network["password"].get<std::string>().c_str();
    Serial.printf("Network SSID: %s\n", ssid.c_str());
    wifiMulti.addAP(ssid.c_str(), password.c_str());
  }

  // Setup MQTT server and callback
  mqttClient.setServer(MQTT_HOST, 1883);
  mqttClient.setCallback(mqttCallback);
}

/******************************************************************************
 *                         HELPER FUNCTIONS
 ******************************************************************************/

// Load WiFi configuration from SPIFFS (JSON file)
nlohmann::json loadWifiConfig(SPIFFSManager& spiffsManager) {
  Serial.begin(115200); // Ensure Serial is initialized
  nlohmann::json wifiJSON = nlohmann::json::array();

  // Check if the WiFi configuration file exists
  if (!spiffsManager.fileExists("/wifi.json")) {
    M5.Display.println("wifi.json does not exist");
    const char* defaultWifi = "[{\"ssid\":\"" WIFI_SSID "\",\"password\":\"" WIFI_PASS "\"}]";
    spiffsManager.writeFile("/wifi.json", defaultWifi);
    M5.Display.println("wifi.json created");
    Serial.printf("Default wifi config written: %s\n", defaultWifi);
    M5.Display.printf("Default wifi config written: %s\n", defaultWifi);
  } else {
    M5.Display.println("wifi.json exists");
  }

  // Read and parse the WiFi configuration file
  String wifis = spiffsManager.readFile("/wifi.json");
  Serial.printf("Read wifi file content: '%s'\n", wifis.c_str());

  if (wifis.length() > 0) {
    try {
      wifiJSON = nlohmann::json::parse(wifis.c_str());
      M5.Display.printf("Loaded %d wifi networks\n", wifiJSON.size());
      // Debug: Print each network's SSID
      for (const auto& network : wifiJSON) {
        Serial.printf("Loaded Network SSID: %s\n", network["ssid"].get<std::string>().c_str());
      }
    } catch (const nlohmann::json::exception& e) {
      Serial.printf("JSON parsing error: %s\n", e.what());
      M5.Display.println("Error parsing wifi config");
    }
  } else {
    Serial.println("No wifi configuration read");
    M5.Display.println("No wifi configuration");
  }

  return wifiJSON;
}

// Display WiFi signal strength icon on the display
void displayWifiStatus() {
  int iconSize = 20;
  int iconX = M5.Display.width() - iconSize; // Top-right corner x-coordinate
  int iconY = 0;                             // Top of the display

  // Clear the icon area
  M5.Display.fillRect(iconX, iconY, iconSize, iconSize, BLACK);

  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    int bars = 0;
    if (rssi >= -50) bars = 4;
    else if (rssi >= -60) bars = 3;
    else if (rssi >= -70) bars = 2;
    else bars = 1;

    // Draw WiFi bars
    for (int i = 0; i < 4; i++) {
      int barWidth = 2;
      int spacing = 2;
      int x = iconX + spacing + i * (barWidth + spacing);
      int baseY = iconY + iconSize - spacing - 6;
      int barHeight = 2 * (i + 1);
      if (i < bars) {
        M5.Display.fillRect(x, baseY - barHeight, barWidth, barHeight, GREEN);
      } else {
        M5.Display.drawRect(x, baseY - barHeight, barWidth, barHeight, WHITE);
      }
    }
  } else {
    // Draw a red "X" when not connected
    int padding = 4;
    int startX = iconX + padding;
    int startY = iconY + padding;
    int endX = iconX + iconSize - padding;
    int endY = iconY + iconSize - padding;
    M5.Display.drawLine(startX, startY, endX, endY, RED);
    M5.Display.drawLine(endX, startY, startX, endY, RED);
  }
}

// Display battery status icon on the display
void displayBatteryStatus() {
  int batteryIconWidth = 24;
  int batteryIconHeight = 14;
  int batteryX = M5.Display.width() - batteryIconWidth - 25; // Positioned to the left of WiFi icon
  int batteryY = 3;

  // Clear battery icon area
  M5.Display.fillRect(batteryX, batteryY, batteryIconWidth, batteryIconHeight, BLACK);

  // Get battery status
  std::int32_t batLevel = M5.Power.getBatteryLevel();   // Percentage 0-100
  int16_t batVoltage = M5.Power.getBatteryVoltage();      // In millivolts

  // Draw battery outline
  int bodyWidth = batteryIconWidth - 4;
  int bodyHeight = batteryIconHeight - 4;
  M5.Display.drawRect(batteryX, batteryY, bodyWidth, bodyHeight, WHITE);
  // Draw the positive terminal
  M5.Display.fillRect(batteryX + bodyWidth, batteryY + (bodyHeight / 2) - 2, 3, 4, WHITE);

  // Fill battery level proportionally
  int fillWidth = ((bodyWidth - 2) * batLevel) / 100;
  M5.Display.fillRect(batteryX + 1, batteryY + 1, fillWidth, bodyHeight - 2, GREEN);

  // Indicate charging if battery voltage is high enough (adjust threshold as needed)
  bool charging = (batVoltage >= 4200);
  if (charging) {
    int centerX = batteryX + bodyWidth / 2;
    int centerY = batteryY + bodyHeight / 2;
    // Draw a simple lightning bolt as a charging symbol
    M5.Display.drawLine(centerX - 4, batteryY + 2, centerX, centerY, YELLOW);
    M5.Display.drawLine(centerX, centerY, centerX - 2, batteryY + bodyHeight - 2, YELLOW);
  }
}

// Connect to WiFi using the configured networks
bool wifiConnect() {
  bool connected = false;

  if (wifiMulti.run() == WL_CONNECTED) { // Successfully connected to WiFi
    connected = true;
    mqttLastReconnectAttempt = 0;
  } else {
    delay(1000);
  }

  // Update display status every 10 seconds
  static unsigned long lastDisplayUpdate = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - lastDisplayUpdate >= 10000) {
    displayWifiStatus();
    displayBatteryStatus();
    lastDisplayUpdate = currentMillis;
  }
  return connected;
}

// Reconnect to the MQTT broker if disconnected
boolean mqttReconnect() {
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD, MQTT_TOPIC, 1, false, "", false)) {
    // Subscribe to the topic once connected
    mqttClient.subscribe(MQTT_TOPIC);
  }
  return mqttClient.connected();
}

// Callback for incoming MQTT messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);
  M5.Display.println(message);
  // Additional processing (e.g., JSON parsing) can be added here
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
        if (mqttReconnect()) {
          mqttLastReconnectAttempt = 0;
        }
      }
    } else {
      // Process incoming MQTT messages
      mqttClient.loop();
    }
  }
}