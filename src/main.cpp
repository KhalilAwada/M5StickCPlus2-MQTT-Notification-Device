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
#define MQTT_MAX_PACKET_SIZE 4096
#include <PubSubClient.h>
#include <iostream>
#include <sstream>
// The canvas functions are provided by the M5GFX engine via M5Unified.
// If needed, you can explicitly include <M5GFX.h> here.
// #include <M5GFX.h>

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
 *                    GLOBAL OBJECTS & VARIABLES
 ******************************************************************************/
SPIFFSManager spiffsManager(SPIFFS);
WiFiClient wifiClient;
WiFiMulti wifiMulti;                 // Manages multiple WiFi networks
PubSubClient mqttClient(wifiClient); // MQTT client using WiFi

// Create a canvas that will print scrollable text.
// The canvas will be drawn starting at y = 25 (to leave room for indicators)
M5Canvas canvas(&M5.Display);

long mqttLastReconnectAttempt = 0;

/******************************************************************************
 *                        FUNCTION PROTOTYPES
 ******************************************************************************/
nlohmann::json loadWifiConfig(SPIFFSManager &spiffsManager);
bool wifiConnect();
boolean mqttReconnect();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void displayWifiStatus();
nlohmann::json updateWifiConfig(SPIFFSManager &spiffsManager, const char *ssid, const char *password);
void displayBatteryStatus();
void displayMQTTStatus();
void handleGithubEventJSON(const nlohmann::json &event);
void handleGithubEvent(String message);
/******************************************************************************
 *                                SETUP
 ******************************************************************************/
void setup()
{
  Serial.begin(115200);

  auto cfg = M5.config();
  // Initialize M5Stack and power management
  M5.begin(cfg);
  { /// I2S Custom configurations are available if you desire.
    auto spk_cfg = M5.Speaker.config();

    spk_cfg.buzzer = true;
    if (spk_cfg.use_dac || spk_cfg.buzzer)
    {
      /// Increasing the sample_rate will improve the sound quality instead of increasing the CPU load.
      spk_cfg.sample_rate = 192000; // default:64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
    }

    M5.Speaker.config(spk_cfg);
  }
  M5.Power.begin();
  M5.Speaker.begin();

  delay(3000);
  Serial.println("Started");

  // Configure primary display type and orientation
  M5.setPrimaryDisplayType({m5::board_t::board_M5UnitLCD});
  M5.Display.setRotation(3); // 90° clockwise rotation

  // Set appropriate text size for the built-in display
  int textSize = M5.Display.height() / 68;
  if (textSize == 0)
  {
    textSize = 1;
  }
  M5.Display.setTextSize(textSize);

  // Initialize SPIFFS
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
  {
    Serial.println("SPIFFS Mount Failed");
    M5.Display.println("SPIFFS Mount Failed");
    return;
  }

  // Load WiFi configuration from file and add networks to WiFiMulti
  nlohmann::json wifiJSON = loadWifiConfig(spiffsManager);
  for (const auto &network : wifiJSON)
  {
    String ssid = network["ssid"].get<std::string>().c_str();
    String password = network["password"].get<std::string>().c_str();
    Serial.printf("Network SSID: %s\n", ssid.c_str());
    wifiMulti.addAP(ssid.c_str(), password.c_str());
  }

  // Setup MQTT server and callback
  mqttClient.setServer(MQTT_HOST, 1883);
  mqttClient.setCallback(mqttCallback);

  /**************************************************************************
   *                Initialize the Scrollable Text Canvas
   **************************************************************************/
  // Create a sprite for the canvas that spans the full display width and
  // the height minus 25px (reserved for the WiFi and battery status).
  // canvas.setColorDepth(1); // Mono color
  canvas.createSprite(M5.Display.width(), M5.Display.height() - 25);
  // Set the text size based on canvas width (adjust scaling factor as needed)
  canvas.setTextSize((float)canvas.width() / 200);
  // Enable automatic vertical scrolling when printing text
  canvas.setTextScroll(true);
}

/******************************************************************************
 *                         HELPER FUNCTIONS
 ******************************************************************************/

nlohmann::json updateWifiConfig(SPIFFSManager &spiffsManager, const char *ssid, const char *password)
{
  nlohmann::json wifiJSON;

  // Check if the file exists.
  if (!spiffsManager.fileExists("/wifi.json"))
  {
    M5.Display.println("wifi.json does not exist, creating new file.");
    wifiJSON = nlohmann::json::array();
  }
  else
  {
    // Read the file content and try to parse it.
    String wifis = spiffsManager.readFile("/wifi.json");
    Serial.printf("Read wifi file content: '%s'\n", wifis.c_str());
    try
    {
      wifiJSON = nlohmann::json::parse(wifis.c_str());
      if (!wifiJSON.is_array())
      {
        M5.Display.println("Config is not an array; creating new array.");
        wifiJSON = nlohmann::json::array();
      }
    }
    catch (const nlohmann::json::exception &e)
    {
      Serial.printf("JSON parsing error: %s\n", e.what());
      M5.Display.println("Error parsing wifi.json; creating new array.");
      wifiJSON = nlohmann::json::array();
    }
  }

  // Look for an existing credential with the same SSID.
  bool found = false;
  for (auto &cred : wifiJSON)
  {
    if (cred.contains("ssid") && cred["ssid"] == ssid)
    {
      // Update the password.
      cred["password"] = password;
      found = true;
      break;
    }
  }

  // If not found, add a new credential object.
  if (!found)
  {
    nlohmann::json newCred = {{"ssid", ssid}, {"password", password}};
    wifiJSON.push_back(newCred);
  }

  // Write back the updated JSON configuration to the file.
  spiffsManager.writeFile("/wifi.json", wifiJSON.dump().c_str());
  M5.Display.printf("Updated wifi config: %s\n", wifiJSON.dump().c_str());
  Serial.printf("Updated wifi config written: %s\n", wifiJSON.dump().c_str());

  return wifiJSON;
}

// Load WiFi configuration from SPIFFS (JSON file)
nlohmann::json loadWifiConfig(SPIFFSManager &spiffsManager)
{
  Serial.begin(115200); // Ensure Serial is initialized
  nlohmann::json wifiJSON = nlohmann::json::array();

  // spiffsManager.deleteFile("/wifi.json");
  wifiJSON = updateWifiConfig(spiffsManager, WIFI_SSID, WIFI_PASS);

  String wifis = spiffsManager.readFile("/wifi.json");
  Serial.printf("Read wifi file content: '%s'\n", wifis.c_str());

  if (wifis.length() > 0)
  {
    try
    {
      wifiJSON = nlohmann::json::parse(wifis.c_str());
      M5.Display.printf("Loaded %d wifi networks\n", wifiJSON.size());
      for (const auto &network : wifiJSON)
      {
        Serial.printf("Loaded Network SSID: %s\n", network["ssid"].get<std::string>().c_str());
      }
    }
    catch (const nlohmann::json::exception &e)
    {
      Serial.printf("JSON parsing error: %s\n", e.what());
      M5.Display.println("Error parsing wifi config");
    }
  }
  else
  {
    Serial.println("No wifi configuration read");
    M5.Display.println("No wifi configuration");
  }
  return wifiJSON;
}

void displayMQTTStatus()
{
  int indicatorSize = 8;     // size of the circle (in pixels)
  int batteryIconWidth = 24; // same as used in displayBatteryStatus()
  // Position it to the left of the battery indicator.
  // Battery indicator's x is: M5.Display.width() - batteryIconWidth - 25.
  // We offset further left by indicatorSize + 5 pixels.
  int x = M5.Display.width() - batteryIconWidth - 25 - indicatorSize - 10;
  int y = 3; // align vertically with the battery indicator

  // Set color: GREEN if MQTT is connected, otherwise RED.
  uint16_t color = mqttClient.connected() ? BLUE : DARKGREY;

  // Draw a filled circle indicator
  M5.Display.fillCircle(x + indicatorSize / 2, y + indicatorSize / 2, indicatorSize / 2, color);
}

// Draw the WiFi signal strength icon in the top-right corner
void displayWifiStatus()
{
  int iconSize = 20;
  int iconX = M5.Display.width() - iconSize; // Top-right corner x-coordinate
  int iconY = 0;                             // Top of the display

  M5.Display.fillRect(iconX, iconY, iconSize, iconSize, BLACK);

  if (WiFi.status() == WL_CONNECTED)
  {
    int rssi = WiFi.RSSI();
    int bars = 0;
    if (rssi >= -50)
      bars = 4;
    else if (rssi >= -60)
      bars = 3;
    else if (rssi >= -70)
      bars = 2;
    else
      bars = 1;

    for (int i = 0; i < 4; i++)
    {
      int barWidth = 2;
      int spacing = 2;
      int x = iconX + spacing + i * (barWidth + spacing);
      int baseY = iconY + iconSize - spacing - 6;
      int barHeight = 2 * (i + 1);
      if (i < bars)
      {
        M5.Display.fillRect(x, baseY - barHeight, barWidth, barHeight, GREEN);
      }
      else
      {
        M5.Display.drawRect(x, baseY - barHeight, barWidth, barHeight, WHITE);
      }
    }
  }
  else
  {
    int padding = 4;
    int startX = iconX + padding;
    int startY = iconY + padding;
    int endX = iconX + iconSize - padding;
    int endY = iconY + iconSize - padding;
    M5.Display.drawLine(startX, startY, endX, endY, RED);
    M5.Display.drawLine(endX, startY, startX, endY, RED);
  }
}

// Draw the battery status icon in the top-right area (to the left of WiFi)
void displayBatteryStatus()
{
  int batteryIconWidth = 24;
  int batteryIconHeight = 14;
  int batteryX = M5.Display.width() - batteryIconWidth - 25; // Positioned to left of WiFi icon
  int batteryY = 3;

  M5.Display.fillRect(batteryX, batteryY, batteryIconWidth, batteryIconHeight, BLACK);

  std::int32_t batLevel = M5.Power.getBatteryLevel(); // Percentage 0-100
  int16_t batVoltage = M5.Power.getBatteryVoltage();  // In millivolts

  int bodyWidth = batteryIconWidth - 4;
  int bodyHeight = batteryIconHeight - 4;
  M5.Display.drawRect(batteryX, batteryY, bodyWidth, bodyHeight, DARKGREY);
  M5.Display.fillRect(batteryX + bodyWidth, batteryY + (bodyHeight / 2) - 2, 3, 4, DARKGREY);

  int fillWidth = ((bodyWidth - 2) * batLevel) / 100;
  M5.Display.fillRect(batteryX + 1, batteryY + 1, fillWidth, bodyHeight - 2, GREEN);

  bool charging = (batVoltage >= 4200);
  if (charging)
  {
    int centerX = batteryX + bodyWidth / 2;
    int centerY = batteryY + bodyHeight / 2;
    M5.Display.drawLine(centerX - 4, batteryY + 2, centerX, centerY, DARKGREEN);
    M5.Display.drawLine(centerX, centerY, centerX - 2, batteryY + bodyHeight - 2, DARKGREEN);
    M5.Display.drawLine(centerX - 3, batteryY + 2, centerX, centerY, DARKGREEN);
    M5.Display.drawLine(centerX + 1, centerY, centerX - 2, batteryY + bodyHeight - 2, DARKGREEN);
  }
}

// Connect to WiFi using the configured networks
bool wifiConnect()
{
  bool connected = false;

  if (wifiMulti.run() == WL_CONNECTED)
  {
    connected = true;
    mqttLastReconnectAttempt = 0;
  }
  else
  {
    delay(1000);
  }

  static unsigned long lastDisplayUpdate = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - lastDisplayUpdate >= 10000)
  { // Every 10 seconds
    displayWifiStatus();
    displayBatteryStatus();
    displayMQTTStatus();
    lastDisplayUpdate = currentMillis;
  }
  return connected;
}

// Reconnect to the MQTT broker if disconnected
// boolean mqttReconnect() {
//   if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD, MQTT_TOPIC, 1, false, "", false)) {
//     mqttClient.subscribe(MQTT_TOPIC);
//   }
//   return mqttClient.connected();
// }

boolean mqttReconnect()
{
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD, MQTT_TOPIC, 1, false, "", false))
  {
    Serial.println("MQTT Connected");
    mqttClient.subscribe(MQTT_TOPIC);
  }
  else
  {
    Serial.println("MQTT Connection failed");
  }
  return mqttClient.connected();
}

// Callback for incoming MQTT messages
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  try
  {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    String message;
    for (unsigned int i = 0; i < length; i++)
    {
      message += (char)payload[i];
    }
    Serial.println(message);

    // check if message is a json object
    // if not, return
    if (message.startsWith("{") && message.endsWith("}"))
    {

      try
      {

        nlohmann::json j = nlohmann::json::parse(message);
        if (j["msgType"] == "event" && j["msgGroup"] == "gh")
        {
          Serial.println("message supported");

          handleGithubEventJSON(j);
        }
        else if (j["msgType"] == "config" && j["msgGroup"] == "wifi")
        {
          Serial.println("message supported");
          // validate json object
          if (j.contains("ssid") && j.contains("password"))
          {
            updateWifiConfig(spiffsManager, j["ssid"].get<std::string>().c_str(), j["password"].get<std::string>().c_str());
            loadWifiConfig(spiffsManager);
          }
          else
          {
            Serial.println("Invalid wifi config message");
          }
        }
        else
        {
          Serial.println("message not supported");
        }
      }
      catch (const std::exception &e)
      {
        std::cerr << e.what() << '\n';
      }
    }
    // e|gh|sssssss|bbbbbb|ps  slice this string by | and get each element
    if (message.indexOf('|'))
    {

      std::string input = message.c_str();
      std::vector<std::string> slices;
      std::istringstream stream(input);
      std::string token;

      // Split the string using '|' as the delimiter
      while (std::getline(stream, token, '|'))
      {
        slices.push_back(token);
      }
      String _type = slices[0].c_str();
      String _group = slices[1].c_str();

      Serial.printf("Type: %s\n", _type.c_str());
      Serial.printf("Group: %s\n", _group.c_str());

      if (_type == "e")
      {
        if (_group == "gh")
        {
          handleGithubEvent(message);
        }
      }
    }

    if (message == "clear")
    {
      canvas.clear();
    }
  }
  catch (const std::exception &e)
  {
    Serial.println("Error");
    Serial.println(e.what());
    std::cerr << e.what() << '\n';
  }

  // Serial.println(message);
  // M5.Display.println(message);
  // Additional processing (e.g., JSON parsing) can be added here
}

/******************************************************************************
 *                         HANDLE GITHUB EVENT
 ******************************************************************************/
void handleGithubEvent(String message)
{
  std::string input = message.c_str();
  std::vector<std::string> slices;
  std::istringstream stream(input);
  std::string token;

  // Split the string using '|' as the delimiter
  while (std::getline(stream, token, '|'))
  {
    slices.push_back(token);
  }
  String _type = slices[0].c_str();
  String _group = slices[1].c_str();

  Serial.printf("Type: %s\n", _type.c_str());
  Serial.printf("Group: %s\n", _group.c_str());
  String _color = slices[2].c_str();
  String _line = slices[3].c_str();
  String _order = slices[4].c_str();

  Serial.printf("Color: %s\n", _color.c_str());
  Serial.printf("Line: %s\n", _line.c_str());
  Serial.printf("Order: %s\n", _order.c_str());
  if (_color == "RED")
  {
    canvas.setTextColor(RED);
  }
  else if (_color == "GREEN")
  {
    canvas.setTextColor(GREEN);
  }
  else if (_color == "YELLOW")
  {
    canvas.setTextColor(YELLOW);
  }
  else if (_color == "CYAN")
  {
    canvas.setTextColor(CYAN);
  }
  else if (_color == "WHITE")
  {
    canvas.setTextColor(WHITE);
  }
  else if (_color == "BLACK")
  {
    canvas.setTextColor(BLACK);
  }
  else if (_color == "ORANGE")
  {
    canvas.setTextColor(ORANGE);
  }
  else if (_color == "DARKGREY")
  {
    canvas.setTextColor(DARKGREY);
  }
  else if (_color == "PURPLE")
  {
    canvas.setTextColor(PURPLE);
  }
  else
  {
    canvas.setTextColor(WHITE);
  }
  if (_order == "1")
  {
    if (_color == "RED")
    {
      // play failure tone using M5.Speaker
      // Failure tone sequence: descending tones
      M5.Speaker.tone(8000, 400); // Tone at 800Hz for 200ms
      delay(250);                // Short pause between tones
      M5.Speaker.tone(6000, 600); // Tone at 600Hz for 300ms
    }
    else if (_color == "GREEN")
    {
      // Success tone sequence: rising tones
      M5.Speaker.tone(8000, 100); // 800Hz for 100ms
      delay(150);
      M5.Speaker.tone(10000, 100); // 1000Hz for 100ms
      delay(150);
      M5.Speaker.tone(12000, 200); // 1200Hz for 200ms
    }
    else
    {
      // Generic beep sequence: two short beeps
      M5.Speaker.tone(5000, 150); // 500Hz for 150ms
      delay(200);
      M5.Speaker.tone(5000, 150); // 500Hz for 150ms
    }
  }
  canvas.printf("%s\n", _line.c_str());
  if (_order == "1")
  {
    int currentY = canvas.getCursorY();
    // Draw a horizontal line from x=0 to canvas.width() at that Y position
    // Optionally add an extra newline if you want spacing after the line
    canvas.printf("---------------------------------\n");
    // canvas.drawLine(0, currentY, canvas.width(), currentY+2, DARKGREY);
  }
  canvas.pushSprite(0, 25);
}
void handleGithubEventJSON(const nlohmann::json &event)
{
  // Implement your handling logic here
  if (event["type"].is_string())
  {
    std::string eventType = event["type"].get<std::string>();
    if (eventType == "workflow_run")
    {
      if (event["status"].is_string())
      {
        if (event["status"] == "queued")
        {
          canvas.setTextColor(YELLOW);
        }
        else if (event["status"] == "in_progress")
        {
          canvas.setTextColor(ORANGE);
        }
        else if (event["status"] == "completed")
        {
          if (event["conclusion"].is_string())
          {
            if (event["conclusion"] == "success")
            {
              canvas.setTextColor(GREEN);
            }
            else if (event["conclusion"] == "cancelled")
            {
              canvas.setTextColor(DARKGREY);
            }
            else
            {
              canvas.setTextColor(RED);
            }
          }
        }
      }
    }
    else if (eventType == "push")
    {
      canvas.setTextColor(CYAN);
    }
    else
    {
      canvas.setTextColor(WHITE);
    }
  }

  if (event["lines"].is_array())
  {
    for (size_t i = 0; i < event["lines"].size(); i++)
    {
      canvas.printf("%s\n", event["lines"][i].get<std::string>().c_str());
    }
  }
  canvas.setTextColor(WHITE);

  // if (event["data"]["image"].is_string()) {
  //   // Display image on the screen
  //   // M5.Display.drawJpgFile(SPIFFS, event["data"]["image"].get<std::string>().c_str());
  // }
}

/******************************************************************************
 *                                MAIN LOOP
 ******************************************************************************/
void loop()
{
  // WiFi & MQTT handling
  if (wifiConnect())
  {
    if (!mqttClient.connected())
    {
      long now = millis();
      if (now - mqttLastReconnectAttempt > 5000)
      {
        mqttLastReconnectAttempt = now;
        if (mqttReconnect())
        {
          mqttLastReconnectAttempt = 0;
        }else{
          Serial.println("MQTT Connection failed");
          sleep(3000);
        }

      }
    }
    else
    {
      mqttClient.loop();
    }
  }

  /**************************************************************************
   *                 Update Scrollable Text on the Canvas
   **************************************************************************/
  // Update the canvas once per second with a new text line.
  // static unsigned long lastCanvasUpdate = 0;
  // if (millis() - lastCanvasUpdate >= 1000) {
  //   static int count = 0;
  //   // Print text to the canvas; the text scrolls automatically if overflowing.
  //   // Set text color to a different color for each line
  //   uint16_t colors[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE};
  //   canvas.setTextColor(colors[count % 7]);
  //   canvas.printf("%04d: Hello, scrollable text!\r\n", count);
  //   // Push the sprite to the display at (0, 25) to leave a 25px margin at the top.
  //   canvas.pushSprite(0, 25);
  //   count++;
  //   lastCanvasUpdate = millis();
  // }
}