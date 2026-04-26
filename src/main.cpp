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
#include <vector>

#ifndef MQTT_TLS
#define MQTT_TLS 0
#endif
#if MQTT_TLS
#include <WiFiClientSecure.h>
#endif

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

#ifndef MQTT_TLS_INSECURE
#define MQTT_TLS_INSECURE 0
#endif

/******************************************************************************
 *                    GLOBAL OBJECTS & VARIABLES
 ******************************************************************************/
SPIFFSManager spiffsManager(LittleFS);

#if MQTT_TLS
WiFiClientSecure wifiClient;
#else
WiFiClient wifiClient;
#endif
WiFiMulti wifiMulti;
PubSubClient mqttClient(wifiClient);
M5Canvas canvas(&M5.Display);
M5Canvas statusBar(&M5.Display);   // double-buffered top status bar (kills flicker)
static constexpr int kStatusBarHeight = 24;
// Dark navy gives the status bar subtle definition vs. the black canvas
// without measurably affecting power (same number of pixels written).
static constexpr uint16_t kStatusBarBG = 0x0841; // ~rgb(8,8,12)
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v1.1"
#endif
long mqttLastReconnectAttempt = 0;
bool isCharging = false;
bool enableDimming = true; // Enable dimming when on battery
const uint32_t connectTimeoutMs = 10000;

// Cached status-bar state. Status bar is only redrawn when one of these
// fields actually changes, so the LCD bus is idle when nothing has moved.
struct StatusBarState {
  bool wifiConnected;
  int  wifiBars;       // 0..4
  bool mqttConnected;
  int  batLevel;       // 0..100
  bool charging;
};
static StatusBarState lastStatus = {false, -1, false, -1, false};

/******************************************************************************
 *                        FUNCTION PROTOTYPES
 ******************************************************************************/
JsonDocument loadWifiConfig(SPIFFSManager &spiffsManager);
bool wifiConnect();
boolean mqttReconnect();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void displayWifiStatus();
JsonDocument updateWifiConfig(SPIFFSManager &spiffsManager, const char *ssid, const char *password);
void displayBatteryStatus();
void displayMQTTStatus();
void handleGithubEventJSON(const JsonDocument &event);
void handleGrafanaEventJSON(const JsonDocument &event);
void scanWifiNetworks();
void drawStatusBar();
void refreshStatusBar(bool force = false);
// In your main loop, check for idle time and dim the screen:

// Global variable to track last brightness change time
unsigned long lastBrightnessChange = 0;
const unsigned long brightnessTimeout = 10000; // 10 seconds at full brightness
const unsigned long fadeDuration = 10000;       // 10 sec fade duration
const uint8_t fullBrightness = 150; // Reduced from 200 to save battery
const uint8_t dimBrightness = 0;

/******************************************************************************
 *                                SETUP
 ******************************************************************************/
void setup()
{
  Serial.begin(115200);
  auto cfg = M5.config();
  M5.begin(cfg);
  { // Configure speaker with custom I2S settings
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.buzzer = true;
    if (spk_cfg.use_dac || spk_cfg.buzzer)
    {
      spk_cfg.sample_rate = 192000;
    }
    M5.Speaker.config(spk_cfg);
  }
  M5.Power.begin();
  // Start the speaker once at boot. The previous code called begin()/end()
  // around every tone which slowed the MQTT callback and could miss notes.
  M5.Speaker.begin();

  delay(3000);
  Serial.println("Started");

  M5.setPrimaryDisplayType({m5::board_t::board_M5UnitLCD});
  M5.Display.setRotation(3);
  M5.Display.setColorDepth(8);
  M5.Display.fillScreen(BLACK);

  /**************************************************************************
   *                Initialize the Status Bar Sprite (double-buffered)
   **************************************************************************/
  statusBar.setColorDepth(8);
  statusBar.createSprite(M5.Display.width(), kStatusBarHeight);
  statusBar.fillSprite(kStatusBarBG);
  statusBar.pushSprite(0, 0);
  // 1-px divider between status bar and message canvas
  M5.Display.drawFastHLine(0, kStatusBarHeight, M5.Display.width(), DARKGREY);

  /**************************************************************************
   *                Initialize the Scrollable Text Canvas
   **************************************************************************/
  canvas.setColorDepth(8);
  canvas.createSprite(M5.Display.width(), M5.Display.height() - (kStatusBarHeight + 1));
  canvas.setFont(&fonts::Font0); // compact 6x8 built-in — fits more text per line
  canvas.setTextSize(1);
  canvas.setTextColor(WHITE);
  canvas.setTextScroll(true);
  canvas.fillSprite(BLACK);
  canvas.pushSprite(0, kStatusBarHeight + 1);

  /**************************************************************************
   *                Boot splash (one-time, ~800 ms)
   **************************************************************************/
  {
    canvas.fillSprite(BLACK);
    canvas.setTextDatum(middle_center);
    int cx = canvas.width() / 2;
    int cy = canvas.height() / 2;
    canvas.setTextColor(CYAN);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString("M5 Notify", cx, cy - 14);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(DARKGREY);
    canvas.drawString(FIRMWARE_VERSION, cx, cy + 14);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(WHITE);
    canvas.pushSprite(0, kStatusBarHeight + 1);
    delay(800);
    canvas.fillSprite(BLACK);
    canvas.pushSprite(0, kStatusBarHeight + 1);
  }

  if (!LittleFS.begin(FORMAT_SPIFFS_IF_FAILED))
  {
    Serial.println("LittleFS Mount Failed");
    canvas.println("FS Mount Failed");
    return;
  }

  WiFi.mode(WIFI_STA);


  JsonDocument wifiJSON = loadWifiConfig(spiffsManager);
  for (JsonObject network : wifiJSON.as<JsonArray>())
  {
    const char *ssid = network["ssid"];
    const char *password = network["password"];
    Serial.printf("Adding Network SSID: >%s<\n", ssid ? ssid : "");
    if (ssid && password) {
      wifiMulti.addAP(ssid, password);
    }
  }

#if MQTT_TLS
#if MQTT_TLS_INSECURE
  // WARNING: skips certificate validation. Acceptable only for local/dev brokers.
  wifiClient.setInsecure();
#else
  // TODO: load broker CA via wifiClient.setCACert(...) for production use.
  // Falling back to insecure mode if no CA is provided.
  wifiClient.setInsecure();
#endif
#endif
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  // PubSubClient defaults to a 256-byte RX/TX buffer, which silently drops
  // any larger MQTT payload. Match the callback cap (4 KB) so big JSON
  // messages actually reach mqttCallback().
  mqttClient.setBufferSize(4096);
  // Give the broker more slack on slow Wi-Fi: 15 s socket timeout, 60 s keepalive.
  mqttClient.setSocketTimeout(15);
  mqttClient.setKeepAlive(60);

  // Connect to Wi-Fi using wifiMulti (connects to the SSID with strongest connection)
  Serial.println("Connecting Wifi...");
  if(wifiMulti.run() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    canvas.setTextColor(GREEN);
    canvas.printf("WiFi connected: %s\n", WiFi.SSID().c_str());
    canvas.setTextColor(WHITE);
    canvas.pushSprite(0, kStatusBarHeight + 1);
  }
  refreshStatusBar(true); // initial paint
}


/******************************************************************************
 *                         HELPER FUNCTIONS
 ******************************************************************************/
JsonDocument updateWifiConfig(SPIFFSManager &spiffsManager, const char *ssid, const char *password)
{
  JsonDocument wifiDoc;
  if (!spiffsManager.fileExists("/wifi.json"))
  {
    canvas.println("wifi.json does not exist, creating new file.");
    wifiDoc.to<JsonArray>(); // create an empty array
  }
  else
  {
    String wifis = spiffsManager.readFile("/wifi.json");
    Serial.printf("Read wifi file content: '%s'\n", wifis.c_str());
    DeserializationError error = deserializeJson(wifiDoc, wifis);
    if (error || !wifiDoc.is<JsonArray>())
    {
      canvas.println("Error parsing wifi.json; creating new array.");
      wifiDoc.clear();
      wifiDoc.to<JsonArray>();
    }
  }
  JsonArray array = wifiDoc.as<JsonArray>();
  bool found = false;
  for (JsonObject cred : array)
  {
    if (cred["ssid"].is<const char *>() && strcmp(cred["ssid"], ssid) == 0)
    {
      cred["password"] = password;
      found = true;
      break;
    }
  }
  if (!found)
  {
    JsonObject newCred = array.add<JsonObject>();
    newCred["ssid"] = ssid;
    newCred["password"] = password;
  }
  String output;
  serializeJson(wifiDoc, output);
  spiffsManager.writeFile("/wifi.json", output.c_str());
  Serial.printf("Updated wifi config written: %s\n", output.c_str());
  return wifiDoc;
}

JsonDocument loadWifiConfig(SPIFFSManager &spiffsManager)
{
  JsonDocument wifiDoc;

  // Only seed the SPIFFS file with env-var defaults the first time around;
  // afterwards we trust the persisted config and don't overwrite it.
  bool needsSeed = !spiffsManager.fileExists("/wifi.json");
  if (!needsSeed)
  {
    String existing = spiffsManager.readFile("/wifi.json");
    if (existing.length() == 0)
    {
      needsSeed = true;
    }
    else
    {
      DeserializationError err = deserializeJson(wifiDoc, existing);
      if (err || !wifiDoc.is<JsonArray>() || wifiDoc.as<JsonArray>().size() == 0)
      {
        needsSeed = true;
        wifiDoc.clear();
      }
    }
  }

  if (needsSeed)
  {
    Serial.println("Seeding /wifi.json with env-var defaults.");
    wifiDoc = updateWifiConfig(spiffsManager, WIFI_SSID, WIFI_PASS);
  }

  for (JsonObject network : wifiDoc.as<JsonArray>())
  {
    Serial.printf("Loaded Network SSID: %s\n", network["ssid"].as<const char *>());
  }
  canvas.printf("Loaded %d wifi networks\n", (int)wifiDoc.as<JsonArray>().size());
  canvas.pushSprite(0, kStatusBarHeight + 1);
  return wifiDoc;
}

// Render the entire status bar into the off-screen sprite, then push it as
// a single transfer. This eliminates the clear-then-redraw flicker the
// previous implementation produced when drawing directly on M5.Display.
void drawStatusBar()
{
  statusBar.fillSprite(kStatusBarBG);

  // ---- Battery icon (right side) ----
  const int batIconW = 24, batIconH = 14;
  const int batX = M5.Display.width() - batIconW - 4;
  const int batY = (kStatusBarHeight - batIconH) / 2;
  const int bodyW = batIconW - 4, bodyH = batIconH;
  uint16_t batColor;
  if (lastStatus.batLevel <= 15)      batColor = RED;
  else if (lastStatus.batLevel <= 30) batColor = YELLOW;
  else                                batColor = GREEN;
  statusBar.drawRect(batX, batY, bodyW, bodyH, DARKGREY);
  statusBar.fillRect(batX + bodyW, batY + (bodyH / 2) - 2, 3, 4, DARKGREY);
  int fillWidth = ((bodyW - 2) * lastStatus.batLevel) / 100;
  if (fillWidth < 0) fillWidth = 0;
  statusBar.fillRect(batX + 1, batY + 1, fillWidth, bodyH - 2, batColor);
  if (lastStatus.charging)
  {
    int cx = batX + bodyW / 2, cy = batY + bodyH / 2;
    statusBar.drawLine(cx - 4, batY + 2, cx, cy, DARKGREEN);
    statusBar.drawLine(cx, cy, cx - 2, batY + bodyH - 2, DARKGREEN);
    statusBar.drawLine(cx - 3, batY + 2, cx, cy, DARKGREEN);
    statusBar.drawLine(cx + 1, cy, cx - 2, batY + bodyH - 2, DARKGREEN);
  }

  // ---- WiFi bars (left of battery) ----
  const int wifiW = 20;
  const int wifiX = batX - wifiW - 6;
  const int wifiY = 0;
  if (lastStatus.wifiConnected)
  {
    for (int i = 0; i < 4; i++)
    {
      int barW = 2, spacing = 2;
      int x = wifiX + spacing + i * (barW + spacing);
      int baseY = wifiY + kStatusBarHeight - spacing - 4;
      int barH = 2 * (i + 1);
      if (i < lastStatus.wifiBars)
        statusBar.fillRect(x, baseY - barH, barW, barH, GREEN);
      else
        statusBar.drawRect(x, baseY - barH, barW, barH, DARKGREY);
    }
  }
  else
  {
    int pad = 4;
    int sx = wifiX + pad, sy = wifiY + pad;
    int ex = wifiX + wifiW - pad, ey = wifiY + kStatusBarHeight - pad;
    statusBar.drawLine(sx, sy, ex, ey, RED);
    statusBar.drawLine(ex, sy, sx, ey, RED);
  }

  // ---- MQTT indicator dot (left of WiFi) ----
  const int dotSize = 8;
  int dotX = wifiX - dotSize - 6;
  int dotY = (kStatusBarHeight - dotSize) / 2;
  uint16_t dotColor = lastStatus.mqttConnected ? CYAN : DARKGREY;
  statusBar.fillCircle(dotX + dotSize / 2, dotY + dotSize / 2, dotSize / 2, dotColor);

  // ---- Left-side label: SSID prefix when connected, else "offline" ----
  // Drawn inside the throttled refresh, so it only repaints when state
  // actually changes. No new ongoing battery cost.
  statusBar.setFont(&fonts::Font0); // small built-in 6x8
  statusBar.setTextDatum(middle_left);
  if (lastStatus.wifiConnected)
  {
    statusBar.setTextColor(WHITE, kStatusBarBG);
    String ssid = WiFi.SSID();
    if (ssid.length() > 9) ssid = ssid.substring(0, 9);
    statusBar.drawString(ssid.c_str(), 4, kStatusBarHeight / 2);
  }
  else
  {
    statusBar.setTextColor(DARKGREY, kStatusBarBG);
    statusBar.drawString("offline", 4, kStatusBarHeight / 2);
  }
  statusBar.setTextDatum(top_left);

  statusBar.pushSprite(0, 0);
}

// Sample current state and only redraw if anything actually changed.
// Cheap to call frequently; the LCD bus stays idle when the bar is stable.
void refreshStatusBar(bool force)
{
  StatusBarState s;
  s.wifiConnected = (WiFi.status() == WL_CONNECTED);
  int rssi = s.wifiConnected ? WiFi.RSSI() : -200;
  s.wifiBars = (rssi >= -50) ? 4
             : (rssi >= -60) ? 3
             : (rssi >= -70) ? 2
             : (rssi >= -85) ? 1
                             : 0;
  s.mqttConnected = mqttClient.connected();
  s.batLevel = (int)M5.Power.getBatteryLevel();
  s.charging = isCharging;

  if (force || memcmp(&s, &lastStatus, sizeof(s)) != 0)
  {
    lastStatus = s;
    drawStatusBar();
  }
}

// Legacy wrappers retained for any external callers; route through the
// change-detected refresh so they don't bypass the cache.
void displayMQTTStatus()    { refreshStatusBar(true); }
void displayWifiStatus()    { refreshStatusBar(true); }
void displayBatteryStatus() { refreshStatusBar(true); }

bool wifiConnect()
{
  static bool wasConnected = false;
  static unsigned long lastReconnectAttempt = 0;
  const unsigned long reconnectIntervalMs = 5000;

  if (WiFi.status() == WL_CONNECTED)
  {
    if (!wasConnected) {
      mqttLastReconnectAttempt = 0;
      canvas.setTextColor(GREEN);
      canvas.printf("WiFi connected: %s\n", WiFi.SSID().c_str());
      canvas.setTextColor(WHITE);
      canvas.pushSprite(0, kStatusBarHeight + 1);
      wasConnected = true;
    }
  }
  else
  {
    wasConnected = false;
    unsigned long now = millis();
    // Throttle reconnect attempts so we don't block the main loop on every
    // pass. wifiMulti.run() with a small timeout will fall through quickly
    // when no network is in range.
    if (now - lastReconnectAttempt >= reconnectIntervalMs)
    {
      lastReconnectAttempt = now;
      wifiMulti.run(1000);
    }
  }

  // Poll the status bar at 2 Hz, but refreshStatusBar() only pushes pixels
  // when an icon's underlying value has changed — so the LCD bus stays
  // idle when the bar is stable. Net battery cost is lower than the old
  // 30-second blind clear-and-redraw.
  static unsigned long lastStatusPoll = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - lastStatusPoll >= 2000)
  {
    refreshStatusBar();
    lastStatusPoll = currentMillis;
  }
  return WiFi.status() == WL_CONNECTED;
}

void scanWifiNetworks() {
  // free memory used by scan
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();      // ensure we’re not already connected
  delay(100);

  Serial.println("Scanning for Wi-Fi networks...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("  No networks found");
  } else {
    for (int i = 0; i < n; i++) {
      // SSID, signal strength and open/protected
      Serial.printf("  %2d: %s  (%d dBm)  %s\n",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    (WiFi.encryptionType(i) == WIFI_AUTH_OPEN)
                      ? "open"
                      : "secured");
      delay(10);
    }
  }
}

boolean mqttReconnect()
{
  // cleanSession=false (8th arg) so the broker retains our session and queues
  // QoS>=1 messages while we're offline. Re-delivered on reconnect.
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD, MQTT_TOPIC, 1, false, "", false))
  {
    Serial.println("MQTT Connected");
    // Subscribe at QoS 1 so the broker actually queues messages for this
    // session while we're disconnected (QoS 0 is fire-and-forget).
    mqttClient.subscribe(MQTT_TOPIC, 1);
    canvas.setTextColor(CYAN);
    canvas.printf("[OK] MQTT %s\n", MQTT_TOPIC);
    canvas.setTextColor(WHITE);
    canvas.pushSprite(0, kStatusBarHeight + 1);
  }
  else
  {
    Serial.println("MQTT Connection failed");
  }
  return mqttClient.connected();
}

/******************************************************************************
 *                          MQTT CALLBACK
 ******************************************************************************/

// Map a color name to an M5GFX color constant. Returns WHITE on miss.
static uint16_t colorFromName(const char *name)
{
  if (!name) return WHITE;
  if (strcmp(name, "RED") == 0) return RED;
  if (strcmp(name, "GREEN") == 0) return GREEN;
  if (strcmp(name, "YELLOW") == 0) return YELLOW;
  if (strcmp(name, "CYAN") == 0) return CYAN;
  if (strcmp(name, "WHITE") == 0) return WHITE;
  if (strcmp(name, "BLACK") == 0) return BLACK;
  if (strcmp(name, "ORANGE") == 0) return ORANGE;
  if (strcmp(name, "DARKGREY") == 0) return DARKGREY;
  if (strcmp(name, "PURPLE") == 0) return PURPLE;
  return WHITE;
}

// Queue a non-blocking notification tone for the given color.
// M5.Speaker.tone(freq, duration, channel, stop_current) with stop_current=false
// queues notes back-to-back without delay() calls.
static void playColorTone(const char *color)
{
  if (!color) return;
  if (strcmp(color, "RED") == 0)
  {
    M5.Speaker.tone(8000, 400, 0, true);
    M5.Speaker.tone(6000, 600, 0, false);
  }
  else if (strcmp(color, "GREEN") == 0)
  {
    M5.Speaker.tone(8000, 100, 0, true);
    M5.Speaker.tone(10000, 100, 0, false);
    M5.Speaker.tone(12000, 200, 0, false);
  }
  else
  {
    M5.Speaker.tone(5000, 150, 0, true);
    M5.Speaker.tone(5000, 150, 0, false);
  }
}

// Handle the legacy pipe-delimited "e|gh|<color>|<line>|<order>" format.
static void handleGithubPipeMessage(char *message)
{
  const int maxTokens = 5;
  char *tokens[maxTokens] = {nullptr};
  int index = 0;
  char *saveptr = nullptr;
  char *token = strtok_r(message, "|", &saveptr);
  while (token != nullptr && index < maxTokens)
  {
    tokens[index++] = token;
    token = strtok_r(nullptr, "|", &saveptr);
  }
  if (index < maxTokens) return;
  if (strcmp(tokens[0], "e") != 0 || strcmp(tokens[1], "gh") != 0) return;

  const char *color = tokens[2];
  const char *line = tokens[3];
  const char *order = tokens[4];

  canvas.setTextColor(colorFromName(color));
  if (order && strcmp(order, "1") == 0)
  {
    playColorTone(color);
  }
  canvas.printf("%s\n", line);
  if (order && strcmp(order, "1") == 0)
  {
    canvas.printf("---------------------------------\n");
  }
  canvas.setTextColor(WHITE);   // restore default so next line isn't tinted
  canvas.pushSprite(0, kStatusBarHeight + 1);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  canvas.setFont(&fonts::Font2); // compact 6x8 built-in — fits more text per line

  // Cap message size to avoid stack blow-up; oversize messages are dropped.
  static constexpr size_t kMaxMessage = 4096;
  if (length >= kMaxMessage)
  {
    Serial.printf("MQTT message too large (%u bytes); dropping.\n", length);
    return;
  }
  std::vector<char> buf(length + 1);
  memcpy(buf.data(), payload, length);
  buf[length] = '\0';
  char *message = buf.data();
  Serial.println(message);

  // Try JSON first; fall back to legacy formats only if parse fails.
  JsonDocument doc;
  DeserializationError jsonErr = deserializeJson(doc, message, length);
  if (!jsonErr)
  {
    // Accept both legacy (msgType/msgGroup) and new (messageType/messageGroup)
    // key names. Group "gh" and "github" are treated as equivalent.
    const char *msgType = doc["msgType"].is<const char *>()
                            ? doc["msgType"].as<const char *>()
                            : (doc["messageType"].is<const char *>() ? doc["messageType"].as<const char *>() : nullptr);
    const char *msgGroup = doc["msgGroup"].is<const char *>()
                             ? doc["msgGroup"].as<const char *>()
                             : (doc["messageGroup"].is<const char *>() ? doc["messageGroup"].as<const char *>() : nullptr);
    const bool isGithubGroup = msgGroup && (strcmp(msgGroup, "gh") == 0 || strcmp(msgGroup, "github") == 0);
    const bool isGrafanaGroup = msgGroup && strcmp(msgGroup, "grafana") == 0;
    if (msgType && isGithubGroup && strcmp(msgType, "event") == 0)
    {
      Serial.println("message supported");
      handleGithubEventJSON(doc);
    }
    else if (msgType && isGrafanaGroup && strcmp(msgType, "event") == 0)
    {
      Serial.println("message supported");
      handleGrafanaEventJSON(doc);
    }
    else if (msgType && msgGroup && strcmp(msgType, "config") == 0 && strcmp(msgGroup, "wifi") == 0)
    {
      Serial.println("message supported");
      if (doc["ssid"].is<const char *>() && doc["password"].is<const char *>())
      {
        updateWifiConfig(spiffsManager, doc["ssid"], doc["password"]);
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
      // Fallback: surface useful JSON fields so the user always sees something.
      const char *fallback = nullptr;
      if (doc["message"].is<const char *>())      fallback = doc["message"].as<const char *>();
      else if (doc["text"].is<const char *>())    fallback = doc["text"].as<const char *>();
      else if (doc["title"].is<const char *>())   fallback = doc["title"].as<const char *>();
      else if (doc["body"].is<const char *>())    fallback = doc["body"].as<const char *>();

      canvas.setTextColor(WHITE);
      if (fallback)
      {
        canvas.printf("%s\n", fallback);
      }
      else
      {
        // Pretty-print compact JSON so the user can debug payload shape.
        String out;
        serializeJson(doc, out);
        if (out.length() > 200) { out.remove(200); out += "..."; }
        canvas.printf("%s\n", out.c_str());
      }
      canvas.pushSprite(0, kStatusBarHeight + 1);
    }
  }
  else if (strchr(message, '|') != nullptr)
  {
    handleGithubPipeMessage(message);
  }
  else if (strcmp(message, "clear") == 0)
  {
    canvas.clear();
    canvas.pushSprite(0, kStatusBarHeight + 1);
  }
  else
  {
    // Plain-text message that isn't JSON, pipe, or "clear" — show it raw.
    canvas.setTextColor(WHITE);
    canvas.printf("%s\n", message);
    canvas.pushSprite(0, kStatusBarHeight + 1);
  }

  M5.Display.setBrightness(fullBrightness);
  lastBrightnessChange = millis(); // reset timeout timer
}

/******************************************************************************
 *              HANDLE GITHUB EVENT (JSON)
 ******************************************************************************/
void handleGithubEventJSON(const JsonDocument &event)
{
  const char *eventType = event["type"].is<const char *>() ? event["type"].as<const char *>() : nullptr;
  const char *glyph = "";
  if (eventType)
  {
    if (strcmp(eventType, "workflow_run") == 0)
    {
      const char *status = event["status"].is<const char *>() ? event["status"].as<const char *>() : nullptr;
      if (status)
      {
        if (strcmp(status, "queued") == 0)
        {
          canvas.setTextColor(YELLOW);
          glyph = "... ";
        }
        else if (strcmp(status, "in_progress") == 0)
        {
          canvas.setTextColor(ORANGE);
          glyph = ">> ";
        }
        else if (strcmp(status, "completed") == 0)
        {
          const char *conclusion = event["conclusion"].is<const char *>() ? event["conclusion"].as<const char *>() : nullptr;
          if (conclusion)
          {
            if (strcmp(conclusion, "success") == 0)
            {
              canvas.setTextColor(GREEN);
              glyph = "OK ";
            }
            else if (strcmp(conclusion, "cancelled") == 0)
            {
              canvas.setTextColor(DARKGREY);
              glyph = "-- ";
            }
            else
            {
              canvas.setTextColor(RED);
              glyph = "X  ";
            }
          }
        }
      }
    }
    else if (strcmp(eventType, "push") == 0)
    {
      canvas.setTextColor(CYAN);
      glyph = "+ ";
    }
    else
    {
      canvas.setTextColor(WHITE);
    }
  }

  // Render text. Prefer the `lines` array (current producer format); fall
  // back to a single `message` string for legacy payloads.
  if (event["lines"].is<JsonArrayConst>())
  {
    JsonArrayConst lines = event["lines"].as<JsonArrayConst>();
    bool first = true;
    for (JsonVariantConst v : lines)
    {
      if (!v.is<const char *>()) continue;
      const char *line = v.as<const char *>();
      if (first)
      {
        canvas.printf("%s%s\n", glyph, line);
        first = false;
      }
      else
      {
        canvas.printf("  %s\n", line);
      }
    }
  }
  else if (event["message"].is<const char *>())
  {
    canvas.printf("%s%s\n", glyph, event["message"].as<const char *>());
  }

  canvas.setTextColor(WHITE);
  canvas.pushSprite(0, kStatusBarHeight + 1);
}

/******************************************************************************
 *              HANDLE GRAFANA EVENT (JSON)
 ******************************************************************************/
// Parse a hex color string like "0xff9966" or "#ff9966". Returns true on
// success and writes a canvas-native color value into `out`.
static bool parseHexColor(const char *s, uint32_t &out)
{
  if (!s) return false;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  else if (s[0] == '#') s += 1;
  if (strlen(s) < 6) return false;
  char *end = nullptr;
  unsigned long v = strtoul(s, &end, 16);
  if (end == s) return false;
  uint8_t r = (v >> 16) & 0xFF;
  uint8_t g = (v >> 8) & 0xFF;
  uint8_t b = v & 0xFF;
  out = canvas.color888(r, g, b);
  return true;
}

void handleGrafanaEventJSON(const JsonDocument &event)
{
  // Defaults if the producer omits colors.
  uint32_t fg = WHITE;
  uint32_t bg = BLACK;
  bool haveBg = false;
  if (event["color"].is<const char *>())
  {
    parseHexColor(event["color"].as<const char *>(), fg);
  }
  if (event["bgColor"].is<const char *>())
  {
    haveBg = parseHexColor(event["bgColor"].as<const char *>(), bg);
  }

  // Optional status glyph so a quick glance shows alert state.
  const char *status = event["status"].is<const char *>() ? event["status"].as<const char *>() : nullptr;
  const char *glyph = "";
  if (status)
  {
    if (strcmp(status, "firing") == 0)        glyph = "!! ";
    else if (strcmp(status, "resolved") == 0) glyph = "OK ";
    else if (strcmp(status, "alerting") == 0) glyph = "!! ";
  }

  if (haveBg) canvas.setTextColor(fg, bg);
  else        canvas.setTextColor(fg);

  if (event["lines"].is<JsonArrayConst>())
  {
    JsonArrayConst lines = event["lines"].as<JsonArrayConst>();
    bool first = true;
    for (JsonVariantConst v : lines)
    {
      if (!v.is<const char *>()) continue;
      const char *line = v.as<const char *>();
      if (first)
      {
        canvas.printf("%s%s\n", glyph, line);
        first = false;
      }
      else
      {
        canvas.printf("  %s\n", line);
      }
    }
  }
  else if (event["message"].is<const char *>())
  {
    canvas.printf("%s%s\n", glyph, event["message"].as<const char *>());
  }

  // Restore default text colors so subsequent prints aren't tinted.
  canvas.setTextColor(WHITE, BLACK);
  canvas.pushSprite(0, kStatusBarHeight + 1);
}

/******************************************************************************
 *                                MAIN LOOP
 ******************************************************************************/
void loop()
{
  // Reduce M5.update() calls - only update every 100ms
  static unsigned long lastM5Update = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - lastM5Update >= 100) {
    M5.update();
    lastM5Update = currentMillis;
    
    // Check if BtnA was pressed to wake up screen
    if (M5.BtnA.wasPressed()) {
      M5.Display.setBrightness(fullBrightness);
      lastBrightnessChange = millis(); // reset timeout timer
    }
  }
    
  unsigned long elapsed = millis() - lastBrightnessChange;

  // Check charging status via the power management chip rather than
  // guessing from a raw battery voltage threshold.
  isCharging = M5.Power.isCharging();
  
  static uint8_t lastBrightness = fullBrightness;
  uint8_t targetBrightness = fullBrightness;
  
  if (isCharging) {
    // Keep screen at full brightness when charging
    targetBrightness = fullBrightness;
  } else {
    // On battery: enable dimming to save power
    if (enableDimming && elapsed > brightnessTimeout) {
      unsigned long fadeTime = elapsed - brightnessTimeout;
      if (fadeTime < fadeDuration) {
        // Compute new brightness linearly between fullBrightness and dimBrightness
        targetBrightness = fullBrightness - ((fullBrightness - dimBrightness) * fadeTime) / fadeDuration;
      } else {
        // Fade completed: set brightness to dim value
        targetBrightness = dimBrightness;
      }
    }
  }
  
  // Only update brightness if it changed (reduces flickering)
  if (targetBrightness != lastBrightness) {
    M5.Display.setBrightness(targetBrightness);
    lastBrightness = targetBrightness;
  }
  if (wifiConnect())
  {
    if (!mqttClient.connected())
    {
      long now = millis();
      if (now - mqttLastReconnectAttempt > 15000) // Increased from 5000 to 15000 (15 seconds)
      {
        mqttLastReconnectAttempt = now;
        if (mqttReconnect())
          mqttLastReconnectAttempt = 0;
        else
        {
          Serial.println("MQTT Connection failed");
        }
      }
    }
    else
    {
      mqttClient.loop();
    }
  }
  
  // Reduce CPU usage when idle
  delay(50);
}