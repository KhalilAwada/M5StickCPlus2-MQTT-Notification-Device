#include <Arduino.h>
#include "SPIFFSManager.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <M5UnitLCD.h>
#include <M5Unified.h>
#include <nlohmann/json.hpp>
#include <PubSubClient.h>

// #include "secrets.h"

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
#define MQTT_PORT 1886//as integer
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

// #ifndef MQTT_KEEPALIVE
// #define MQTT_KEEPALIVE 1000
// #endif

SPIFFSManager spiffsManager(SPIFFS);
WiFiClient wifiClient;   
WiFiMulti wifiMulti;           // Create a WiFi client instance
PubSubClient mqttClient(wifiClient); // Declare PubSubClient using the WiFi client

long mqttLastReconnectAttempt = 0;

nlohmann::json loadWifiConfig(SPIFFSManager& spiffsManager);
bool wifiConnect();
boolean mqttReconnect();
void mqttCallback(char* topic, byte* payload, unsigned int length);

void setup()
{
  Serial.begin(115200);

  M5.begin();
  sleep(3);
  Serial.println("started ");  

  M5.setPrimaryDisplayType({m5::board_t::board_M5UnitLCD});
  M5.Display.setRotation(3); // rotates the display 90 degrees clockwise

  int textsize = M5.Display.height() / 68;
  if (textsize == 0)
  {
    textsize = 1;
  }
  M5.Display.setTextSize(textsize);

  // Initialize SPIFFS
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
  {
    Serial.println("SPIFFS Mount Failed");
    M5.Display.println("SPIFFS Mount Failed");
    return;
  }

  // Load WiFi configuration
  nlohmann::json wifiJSON = loadWifiConfig(spiffsManager);

  // loop wifijson
  for (const auto& network : wifiJSON) {
    Serial.printf("Network SSID: %s\n", network["ssid"].get<std::string>().c_str());
    
    wifiMulti.addAP(network["ssid"].get<std::string>().c_str(),network["password"].get<std::string>().c_str());
  }

  mqttClient.setServer(MQTT_HOST, 1883);
  mqttClient.setCallback(mqttCallback);

}

// Add this function implementation after setup()
nlohmann::json loadWifiConfig(SPIFFSManager& spiffsManager) {
  Serial.begin(115200); // Ensure Serial is initialized
  nlohmann::json wifiJSON = nlohmann::json::array();

  // spiffsManager.deleteFile("/wifi.json");
  // Check if wifi file exists
  if (!spiffsManager.fileExists("/wifi.json")) {
    M5.Display.println("wifi.json does not exist");
    const char* defaultWifi = "[{\"ssid\":\"" WIFI_SSID "\",\"password\":\"" WIFI_PASS "\"}]";
    spiffsManager.writeFile("/wifi.json", defaultWifi);
    M5.Display.println("wifi.json created");
    Serial.printf("Default wifi config written: %s\n", defaultWifi);
    M5.Display.printf("Default wifi config written: %s\n", defaultWifi);
  }else{
    M5.Display.println("wifi.json exists");
  }

  // load wifi file
  String wifis = spiffsManager.readFile("/wifi.json");
  Serial.printf("Read wifi file content: '%s'\n", wifis.c_str());
  Serial.printf("Read wifi file content: '%s'\n", wifis);
  
  // convert wifis to json
  if (wifis.length() > 0) {
    try {
      wifiJSON = nlohmann::json::parse(wifis.c_str());
      M5.Display.printf("loaded %d wifi networks\n", wifiJSON.size());
      Serial.printf("Loaded %d wifi networks\n", wifiJSON.dump().size());  
      // Print each network for debugging
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

void displayWifiStatus() {
  int iconSize = 20; 
  int iconX = M5.Display.width() - iconSize; // top right corner x coordinate
  int iconY = 0;                             // top of the display

  // Clear the icon area in the top-right corner
  M5.Display.fillRect(iconX, iconY, iconSize, iconSize, BLACK);

  if (WiFi.status() == WL_CONNECTED) {
      int rssi = WiFi.RSSI();
      int bars = 0;
      if (rssi >= -50) bars = 4;
      else if (rssi >= -60) bars = 3;
      else if (rssi >= -70) bars = 2;
      else bars = 1;

      // Draw 4 bars (smaller for the icon area)
      for (int i = 0; i < 4; i++) {
          int barWidth = 3; 
          int spacing = 2;
          // Calculate x relative to the icon's top-right area
          int x = iconX + spacing + i * (barWidth + spacing);
          int baseY = iconY + iconSize - spacing; // bottom of the icon area
          int barHeight = 2 * (i + 1); // smaller bar height scaling
          if (i < bars) {
              M5.Display.fillRect(x, baseY - barHeight, barWidth, barHeight, GREEN);
          } else {
              M5.Display.drawRect(x, baseY - barHeight, barWidth, barHeight, WHITE);
          }
      }
  } else {
      // Draw a red "X" in the icon area when not connected.
      int padding = 4;
      int startX = iconX + padding;
      int startY = iconY + padding;
      int endX = iconX + iconSize - padding;
      int endY = iconY + iconSize - padding;
      M5.Display.drawLine(startX, startY, endX, endY, RED);
      M5.Display.drawLine(endX, startY, startX, endY, RED);
  }
}

bool wifiConnect(){
  bool connected = false;

  // int n = WiFi.scanNetworks();
  // Serial.println("Scanned Networks:");
  // for (int i = 0; i < n; ++i) {
  //   Serial.printf("SSID: '%s'\n", WiFi.SSID(i).c_str());
  // }
  if (wifiMulti.run() ==
        WL_CONNECTED) {  // If the connection to wifi is established
                         // successfully.  如果与wifi成功建立连接

        // M5.Display.setCursor(0, 20);
        // M5.Display.print("WiFi connected\n\nSSID:");
        // M5.Display.println(WiFi.SSID());  // Output Network name.  输出网络名称
        // M5.Display.print("RSSI: ");
        // M5.Display.println(WiFi.RSSI());  // Output signal strength.  输出信号强度
        // M5.Display.print("IP address: ");
        // M5.Display.println(WiFi.localIP());  // Output IP Address.  输出IP地址
        // M5.Display.println(WiFi.dnsIP());  // Output IP Address.  输出IP地址
        // delay(1000);
        // M5.Display.fillRect(0, 20, 180, 300,
        //                 BLACK);  // It's equivalent to partial screen clearance.
                                 // 相当于部分清屏
        connected = true;
        mqttLastReconnectAttempt = 0;
    } else {
        // If the connection to wifi is not established successfully.
        // 如果没有与wifi成功建立连接
        // M5.Display.print(".");
        delay(1000);
    }
    static unsigned long lastDisplayUpdate = 0;
    unsigned long currentMillis = millis();
    if (currentMillis - lastDisplayUpdate >= 10000) { // 10 seconds
      displayWifiStatus();
      lastDisplayUpdate = currentMillis;
    }
  return connected;
}


boolean mqttReconnect() {
  // print out the type of all the variables starting with MQTT_

  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD, MQTT_TOPIC, 1, false, "", false)) {
    // Once connected, publish an announcement...
    // mqttClient.publish("outTopic","hello world");
    // ... and resubscribe
    mqttClient.subscribe(MQTT_TOPIC);
  }
  return mqttClient.connected();
  return false;
}


// Callback function for handling incoming messages
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
  // Additional processing (e.g., JSON parsing) can go here
}

void loop()
{
  // put your main code here, to run repeatedly:
  if(wifiConnect()){
    if (!mqttClient.connected()) {
      long now = millis();
      if (now - mqttLastReconnectAttempt > 5000) {
        mqttLastReconnectAttempt = now;
        // Attempt to reconnect
        if (mqttReconnect()) {
          mqttLastReconnectAttempt = 0;
        }
      }
    } else {
      // Client connected
  
      mqttClient.loop();
    }
  }
}
