#include <Arduino.h>
#include "SPIFFSManager.h"
#include <M5UnitLCD.h>
#include <M5Unified.h>
#include <nlohmann/json.hpp>


SPIFFSManager spiffsManager(SPIFFS);

void setup()
{
  Serial.begin(115200);

  M5.begin();

  M5.setPrimaryDisplayType({m5::board_t::board_M5UnitLCD});
  M5.Display.setRotation(3); // rotates the display 90 degrees clockwise

  int textsize = M5.Display.height() / 60;
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

  // Check if wifi file exists
  if (!spiffsManager.fileExists("/wifi.json"))
  {
    M5.Display.println("wifi.json does not exist");
    const char* defaultWifi = "[{\"ssid\":\"ssid\",\"password\":\"pass\"}]";
    spiffsManager.writeFile("/wifi.json", defaultWifi);
    M5.Display.println("wifi.json created");
    Serial.printf("Default wifi config written: %s\n", defaultWifi);
  }

  nlohmann::json wifiJSON = nlohmann::json::array();

  // load wifi file
  String wifis = spiffsManager.readFile("/wifi.json");
  Serial.printf("Read wifi file content: '%s'\n", wifis.c_str());
  
  // convert wifis to json
  if (wifis.length() > 0) {
    try {
      wifiJSON = nlohmann::json::parse(wifis.c_str());
      M5.Display.printf("loaded %d wifi networks\n", wifiJSON.size());
      
      // Print each network for debugging
      for (const auto& network : wifiJSON) {
        Serial.printf("Network SSID: %s\n", network["ssid"].get<std::string>().c_str());
      }
    } catch (const nlohmann::json::exception& e) {
      Serial.printf("JSON parsing error: %s\n", e.what());
      M5.Display.println("Error parsing wifi config");
    }
  } else {
    Serial.println("No wifi configuration read");
    M5.Display.println("No wifi configuration");
  }

  // loop wifijson
  for (const auto& network : wifiJSON) {
    Serial.printf("Network SSID: %s\n", network["ssid"].get<std::string>().c_str());
  }

//print number of wifi networks
  // M5.Display.printf("loaded %d wifi networks", wifiJSON.size());

  // List directory contents
  // spiffsManager.listDir("/", 0);

  // Write to a file
  // spiffsManager.writeFile("/hello.txt", "Hello World!");

  // Read from a file
  // spiffsManager.readFile("/hello.txt");

  // Append to a file
  // spiffsManager.appendFile("/hello.txt", " This is an appended line.");

  // Rename a file
  // spiffsManager.renameFile("/hello.txt", "/renamed.txt");

  // Read from the renamed file
  // spiffsManager.readFile("/renamed.txt");

  // Delete the file
  // spiffsManager.deleteFile("/renamed.txt");

  // Test file I/O performance
  // spiffsManager.testFileIO("/test.txt");

  // Clean up the test file
  // spiffsManager.deleteFile("/test.txt");

  // Serial.println("All SPIFFS operations completed successfully!");
}

void loop()
{
  // put your main code here, to run repeatedly:
}
