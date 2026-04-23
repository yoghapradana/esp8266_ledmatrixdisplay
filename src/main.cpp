#include <Arduino.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WiFiClient.h>
#include <ArduinoOTA.h>
#include <TimeLib.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>

#include "config.h"
#include "localization.h"

// Create display instance
MD_Parola display = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// WiFi Credentials (stored in EEPROM)
String savedSSID = "";
String savedPassword = "";

// AsyncWebServer on Port 80
AsyncWebServer server(80);

// OTA Update Status
bool otaEnabled = false;
bool rebootReq = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, 0, 60000); // Initial offset is UTC, will be updated from settings
DisplaySettings settings;                           // Global settings struct

// Variables
char timeStr[7] = "00:00"; // HH:MM format with blinking colon + extra char for a/p indicator
char dateStr[120] = "";    // For full date text
bool colonVisible = true;
bool otaActive = false;
bool showingDate = false;
bool showingCustomMessage = false;
bool reqHijriFetch = false;
String customMessage = "";
uint8_t customMessageRotations = 0;
uint8_t currentRotation = 0;
unsigned long lastTick = 0;
unsigned long lastDateDisplay = 0;
unsigned long lastHijriUpdate = 0;
uint8_t dotAnimationPos = 0;
const char *dotAnimation[] = {".   ", "..  ", "... ", "...."}; // Dot animation frames
unsigned long timeOut = 0;
bool initialFetchDone = false;

// Islamic date variables
String hijriDate = "Null"; // Full date (e.g. "27-09-1446")
int hijridates[3];         // Day only (e.g. "27")
String hijriMonth = "";    // Month name (e.g. "Ramadan")
String hijriYear = "";     // Year (e.g. "1446")

// ========================================== //
// ===== FORWARD DECLARATIONS (PROTOTYPES) == //
// ========================================== //
void showStatusMessage(const char *msg, textPosition_t alignment = PA_LEFT);
void loadSettings();
void saveSettings();
float calculateSunEvent(float lat, float lon, int dayOfYear, bool isSunrise, int timeOffset);
void updateBrightness();
void loadCredentials();
void saveCredentials(String ssid, String password);
void startAPMode();
void handleAPMode();
String htmlProcessor(const String &var);
void showClock();
void showDate();
void showCustomMessage();
void animateClock();
void updateTime();
void animateDate();
void updateDate();
bool fetchHijriDate();
void wifiConnectingAnimation();
void setupOTA();
void setupWebServer();

// ========================================== //
// ===== MAIN SETUP & LOOP ================== //
// ========================================== //

void setup()
{
  display.begin();
  showStatusMessage("BOOT", PA_LEFT);

  EEPROM.begin(512);

  if (!LittleFS.begin())
  {
    showStatusMessage("FS ERR", PA_LEFT);
    // Serial.println("An Error has occurred while mounting LittleFS");
  }

  loadSettings();
  updateBrightness();
  loadCredentials();

  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  unsigned long wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 20000)
  {
    wifiConnectingAnimation();
    delay(300);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    showStatusMessage("FAIL", PA_LEFT);
    delay(2000);
    startAPMode();
    timeOut = millis();
  }
  else
  {
    setupOTA();
    otaEnabled = true;
  }

  setupWebServer();

  timeClient.begin();
  timeClient.forceUpdate();
  fetchHijriDate();
  showStatusMessage("SYNC", PA_LEFT);

  MDNS.begin("leddisplay");

  showStatusMessage("READY", PA_LEFT);
  delay(1000);
}

void loop()
{
  MDNS.update();

  if (otaEnabled)
  {
    ArduinoOTA.handle();
  }

  if (rebootReq)
  {
    delay(1000);
    ESP.restart();
  }

  if (WiFi.getMode() == WIFI_AP)
  {
    handleAPMode();
    return;
  }

  if (otaActive)
  {
    return;
  }

  // 1. Trigger at Midnight
  bool isMidnight = (timeClient.getHours() == 0 && timeClient.getMinutes() == 0 && timeClient.getSeconds() == 0);

  // 2. Trigger on Reset (only once after NTP is valid)
  bool isInitialSync = (!initialFetchDone && timeClient.isTimeSet());

  // --- THE HIJRI FETCH BLOCK ---
  if (isMidnight || isInitialSync || reqHijriFetch)
  {
    static int lastFetchedDay = -1;       // Guard variable
    int currentDay = timeClient.getDay(); // Or day() from TimeLib

    // ONLY fetch if it's a new day, OR if we manually requested via Web/Initial Sync
    if (currentDay != lastFetchedDay || reqHijriFetch || isInitialSync)
    {
      if (fetchHijriDate())
      {
        initialFetchDone = true;
        reqHijriFetch = false;
        lastFetchedDay = currentDay; // Mark this day as done
        // Serial.println("Hijri Updated Successfully");
      }
      else
      {
        // If it fails, we don't update lastFetchedDay,
        // so it will try again on the next loop iteration.
        // Serial.println("Hijri Fetch Failed. Retrying...");
      }
    }
  }
  // --- END HIJRI FETCH BLOCK ---

  if (showingCustomMessage)
  {
    if (display.displayAnimate())
    {
      currentRotation++;
      if (currentRotation >= customMessageRotations)
      {
        showingCustomMessage = false;
        customMessage = "";
        currentRotation = 0;
        lastDateDisplay = millis();
      }
      else
      {
        showCustomMessage();
      }
    }
    return;
  }

  if (showingDate)
  {
    if (display.displayAnimate())
    {
      showingDate = false;
      lastDateDisplay = millis();
    }
    return;
  }

  if (millis() - lastTick >= TICK_INTERVAL)
  {
    lastTick = millis();
    animateClock();
    showClock();

    static uint8_t lastHour = 255;
    if (hour() != lastHour)
    {
      lastHour = hour();
      updateBrightness();
    }
  }

  if (millis() - lastDateDisplay >= DATE_DISPLAY_INTERVAL)
  {
    showingDate = true;
    animateDate();
    showDate();
  }
}

// ========================================== //
// ===== UTILITY & SETTINGS FUNCTIONS ======= //
// ========================================== //

void showStatusMessage(const char *msg, textPosition_t alignment)
{
  display.setFont(nullptr); // Use default font for status messages
  display.displayClear();
  display.setTextAlignment(alignment);
  display.print(msg);
}

// Load settings from EEPROM (bytes 96-120)
void loadSettings()
{
  EEPROM.get(96, settings);

  // Validate settings
  if (settings.dayBrightness > 15)
    settings.dayBrightness = 8;
  if (settings.nightBrightness > 15)
    settings.nightBrightness = 1;
  if (settings.timeOffset < -43200 || settings.timeOffset > 50400)
    settings.timeOffset = 0;
  if (settings.hijriOffset < -2 || settings.hijriOffset > 2)
    settings.hijriOffset = 0;

  // Update NTP client with loaded offset
  timeClient.setTimeOffset(settings.timeOffset);
}

// Save settings to EEPROM
void saveSettings()
{
  EEPROM.put(96, settings);
  EEPROM.commit();
}

// Returns hour of sunrise or sunset for given day of year
float calculateSunEvent(float lat, float lon, int dayOfYear, bool isSunrise, int timeOffset)
{
  // Simplified calculation - good enough for brightness control
  float declination = -23.45 * cos(2 * PI * (dayOfYear + 10) / 365.0);
  float hourAngle = acos(-tan(lat * PI / 180.0) * tan(declination * PI / 180.0)) * 180.0 / PI;

  float solarNoon = 12.0 + (timeOffset / 3600) - (lon / 15.0);

  if (isSunrise)
  {
    return solarNoon - (hourAngle / 15.0);
  }
  else
  {
    return solarNoon + (hourAngle / 15.0);
  }
}

void updateBrightness()
{
  uint8_t targetBrightness;
  int currentHour = hour();
  int currentMinute = minute();
  float currentTime = currentHour + (currentMinute / 60.0);

  if (settings.brightnessMode)
  {
    // Auto mode: use sunrise/sunset times based on location
    int dayOfYear = day() + (month() - 1) * 30; // Approximation
    float sunrise = calculateSunEvent(settings.latitude, settings.longitude, dayOfYear, true, settings.timeOffset);
    float sunset = calculateSunEvent(settings.latitude, settings.longitude, dayOfYear, false, settings.timeOffset);

    if (currentTime >= sunrise && currentTime < sunset)
    {
      targetBrightness = settings.dayBrightness;
    }
    else
    {
      targetBrightness = settings.nightBrightness;
    }
  }
  else
  {
    // Manual mode: use user-defined start hours
    int dayStart = settings.dayStartHour;
    int nightStart = settings.nightStartHour;

    // Handle wrap-around (e.g., day 6-18, or day 22-8)
    if (dayStart < nightStart)
    {
      if (currentHour >= dayStart && currentHour < nightStart)
      {
        targetBrightness = settings.dayBrightness;
      }
      else
      {
        targetBrightness = settings.nightBrightness;
      }
    }
    else
    {
      if (currentHour >= dayStart || currentHour < nightStart)
      {
        targetBrightness = settings.dayBrightness;
      }
      else
      {
        targetBrightness = settings.nightBrightness;
      }
    }
  }

  display.setIntensity(targetBrightness);
}

void loadCredentials()
{
  savedSSID = "";
  savedPassword = "";

  // Read SSID (first 32 bytes)
  for (int i = 0; i < 32; ++i)
  {
    char c = EEPROM.read(i);
    if (c == 0)
      break;
    savedSSID += c;
  }

  // Read Password (next 64 bytes)
  for (int i = 32; i < 96; ++i)
  {
    char c = EEPROM.read(i);
    if (c == 0)
      break;
    savedPassword += c;
  }

  showStatusMessage("OK", PA_LEFT);
}

void saveCredentials(String ssid, String password)
{
  // Clear EEPROM
  for (int i = 0; i < 96; ++i)
  {
    EEPROM.write(i, 0);
  }

  // Write SSID
  for (unsigned int i = 0; i < ssid.length(); ++i)
  {
    EEPROM.write(i, ssid[i]);
  }
  EEPROM.write(ssid.length(), 0);

  // Write Password
  for (unsigned int i = 0; i < password.length(); ++i)
  {
    EEPROM.write(32 + i, password[i]);
  }
  EEPROM.write(32 + password.length(), 0);

  EEPROM.commit();
  showStatusMessage("OK", PA_LEFT);
}

// ========================================== //
// ===== DISPLAY & ANIMATION FUNCTIONS ====== //
// ========================================== //

void showClock()
{
  display.setTextAlignment(PA_CENTER);
  display.print(timeStr);
}

void showDate()
{
  display.setFont(nullptr);
  display.displayClear();
  display.setTextAlignment(PA_LEFT);
  display.displayScroll(dateStr, PA_LEFT, PA_SCROLL_LEFT, SCROLL_SPEED);
}

void showCustomMessage()
{
  display.setFont(nullptr);
  display.displayClear();
  display.setTextAlignment(PA_LEFT);
  display.displayScroll(customMessage.c_str(), PA_LEFT, PA_SCROLL_LEFT, SCROLL_SPEED);
}

void animateClock()
{
  colonVisible = !colonVisible;

  int h = hour();
  char ampm = (h >= 12) ? 'p' : 'a'; // Determine AM/PM
  if (!settings.is24h)
  {
    h = h % 12;
    if (h == 0)
      h = 12;

    // Format: "12:05a" or "12 05a"
    // Using a narrow font helps this fit on 32 columns
    snprintf(timeStr, sizeof(timeStr), "%d%c%02d%c",
             h,
             colonVisible ? ':' : ' ',
             minute(),
             ampm);
  }
  else
  {
    // Standard 24h: "23:05"
    snprintf(timeStr, sizeof(timeStr), "%02d%c%02d",
             h,
             colonVisible ? ':' : ' ',
             minute());
  }
}

void animateDate()
{
  time_t epochTime = timeClient.getEpochTime();
  unsigned long daysSinceEpoch = epochTime / 86400;

  String datePart = String(dayWeek[weekday() - 1]);

  if (settings.showPasaran)
  {
    datePart += " " + String(dayPasaran[daysSinceEpoch % 5]);
  }

  datePart += ", " + String(day()) + " " + String(monthNames[month() - 1]) + " " + String(year());

  if (settings.showHijri && hijridates[0] > 0)
  {
    datePart += " / " + String(hijridates[0]) + " " + String(hijriMonths[hijridates[1] - 1]) + " " + String(hijridates[2]) + "H";
  }

  datePart.toCharArray(dateStr, sizeof(dateStr));
}

// ========================================== //
// ===== TIME & DATE FUNCTIONS ============== //
// ========================================== //

void updateTime()
{
  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  setTime(epochTime);

  snprintf(timeStr, sizeof(timeStr), "%02d%c%02d",
           hour(),
           colonVisible ? ':' : ' ',
           minute());
}

void updateDate()
{
  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  setTime(epochTime);
  unsigned long daysSinceEpoch = epochTime / 86400;

  snprintf(dateStr, sizeof(dateStr), "%s %s, %d %s %d / %d %s %dH",
           dayWeek[weekday() - 1],
           dayPasaran[daysSinceEpoch % 5],
           day(),
           monthNames[month() - 1],
           year(),
           hijridates[0],
           hijriMonths[hijridates[1] - 1],
           hijridates[2]);
}

bool fetchHijriDate()
{
  WiFiClient client;
  HTTPClient http;

  // 1. Calculate the 'Target Date' for the API
  // Instead of asking for 'Today', we ask for 'Today + Offset'
  updateTime(); // Make sure we have the latest time
  time_t now = timeClient.getEpochTime();
  now += (settings.hijriOffset * 86400); // Add/Subtract days in seconds

  struct tm *t = localtime(&now);

  char dateBuffer[11];
  strftime(dateBuffer, sizeof(dateBuffer), "%d-%m-%Y", t);

  String url = String(HIJRI_API_URL) + "/" + String(dateBuffer);

  if (http.begin(client, url))
  {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (!error)
      {
        JsonObject hijri = doc["data"]["hijri"];
        hijriDate = hijri["date"].as<String>();
        hijridates[0] = hijri["day"];
        hijridates[1] = hijri["month"]["number"];
        hijridates[2] = hijri["year"];

        http.end();
        return true; // Successfully updated Hijri date
      }
    }
    http.end();
  }
  return false; // Failed to fetch or parse Hijri date
}

// ========================================== //
// ===== NETWORK & WEB FUNCTIONS ============ //
// ========================================== //

void startAPMode()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
}

void handleAPMode()
{
  static String apMessage = "";

  if (apMessage.length() == 0)
  {
    IPAddress apIP = WiFi.softAPIP();
    apMessage = "WiFi: " + String(AP_SSID) + " | Password: " + String(AP_PASSWORD) + " | Config: http://" + apIP.toString();
  }

  if (display.displayAnimate())
  {
    display.displayScroll(apMessage.c_str(), PA_LEFT, PA_SCROLL_LEFT, SCROLL_SPEED);
  }

  if (millis() - timeOut >= AP_MODE_TIMEOUT)
  {
    rebootReq = true;
  }
}

void wifiConnectingAnimation()
{
  showStatusMessage(dotAnimation[dotAnimationPos], PA_LEFT);
  dotAnimationPos = (dotAnimationPos + 1) % 4;
}

void setupOTA()
{
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]()
                     {
    otaActive = true;
    showStatusMessage("OTA", PA_LEFT); });

  ArduinoOTA.onEnd([]()
                   {
    showStatusMessage("DONE", PA_LEFT);
    delay(1000);
    ESP.restart(); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
    char progressMsg[5];
    snprintf(progressMsg, sizeof(progressMsg), "%2d%%", (progress * 100) / total);
    showStatusMessage(progressMsg, PA_LEFT); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    const char *errorMsg = "ERR";
    if (error == OTA_AUTH_ERROR) errorMsg = "AUTH";
    else if (error == OTA_BEGIN_ERROR) errorMsg = "BEGN";
    showStatusMessage(errorMsg, PA_LEFT);
    delay(2000); });

  ArduinoOTA.begin();
}

String htmlProcessor(const String &var)
{
  if (var == "CURRENT_SSID")
    return savedSSID.length() ? savedSSID : "Not set";

  if (var == "WIFI_STATUS")
  {
    if (WiFi.status() == WL_CONNECTED)
      return "Connected (" + WiFi.localIP().toString() + ")";
    else if (savedSSID.length())
      return "Saved but not connected";
    else
      return "No saved network";
  }

  if (var == "DAY_BRIGHTNESS")
    return String(settings.dayBrightness);
  if (var == "NIGHT_BRIGHTNESS")
    return String(settings.nightBrightness);
  if (var == "DAY_START_HOUR")
    return String(settings.dayStartHour);
  if (var == "NIGHT_START_HOUR")
    return String(settings.nightStartHour);
  if (var == "LATITUDE")
    return String(settings.latitude, 4);
  if (var == "LONGITUDE")
    return String(settings.longitude, 4);
  if (var == "TIME_OFFSET")
    return String(settings.timeOffset);
  if (var == "HIJRI_OFFSET")
    return String(settings.hijriOffset);

  if (var == "TIME_OFFSET_HOURS")
  {
    float offsetHours = settings.timeOffset / 3600.0;
    String s = (offsetHours >= 0 ? "+" : "");
    return s + String(offsetHours, 1);
  }

  return String();
}

void setupWebServer()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    // 1. File System (LittleFS)
    // 2. File Path ("/index.html")
    // 3. Content-Type + Charset ("text/html; charset=utf-8")
    // 4. Download flag (false = display in browser, true = force download)
    // 5. Template processor (htmlProcessor)
    
    request->send(LittleFS, "/index.html", "text/html; charset=utf-8", false, htmlProcessor); });

  // serve static files
  server.serveStatic("/", LittleFS, "/");

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String newSSID = request->getParam("ssid", true)->value();
      String newPassword = request->getParam("password", true)->value();

      saveCredentials(newSSID, newPassword);
      request->send(200, "text/plain", "OK");
      rebootReq = true;
    } else {
      request->send(400, "text/plain", "Bad Request");
    } });

  server.on("/show", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("message")) {
      customMessage = request->getParam("message")->value();

      customMessageRotations = 2;
      if (request->hasParam("rotations")) {
        customMessageRotations = request->getParam("rotations")->value().toInt();
        if (customMessageRotations < 1) customMessageRotations = 1;
        if (customMessageRotations > 10) customMessageRotations = 10;
      }

      customMessage.replace("+", " ");
      customMessage.replace("%20", " ");

      showingCustomMessage = true;
      currentRotation = 0;
      showCustomMessage();

      request->send(200, "text/plain", "Message set: " + customMessage);
    } else {
      request->send(400, "text/plain", "Missing 'message' parameter.");
    } });

  server.on(

      "/settings", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      {
        JsonDocument doc;
        // Using 'total' ensures we consider the expected length of the body
        DeserializationError error = deserializeJson(doc, data, len);

        if (!error)
        {
          // 1. Brightness Mode (Boolean)
          if (doc["brtMode"].is<bool>())
          {
            settings.brightnessMode = doc["brtMode"];
          }

          // 2. Numeric & Bool values
          if (doc["tmOft"].is<int>())
          {
            settings.timeOffset = doc["tmOft"];
            timeClient.setTimeOffset(settings.timeOffset);
          }
          if (doc["hjrOft"].is<int>())
          {
            int newHijriOffset = doc["hjrOft"];
            if (settings.hijriOffset != newHijriOffset)
            {
              settings.hijriOffset = newHijriOffset;
              reqHijriFetch = true; // Trigger a fetch with the new offset
            }
          }
          if (doc["dayBrt"].is<int>())
            settings.dayBrightness = doc["dayBrt"];

          if (doc["nightBrt"].is<int>())
            settings.nightBrightness = doc["nightBrt"];

          if (doc["dSH"].is<int>())
            settings.dayStartHour = doc["dSH"];

          if (doc["nSN"].is<int>())
            settings.nightStartHour = doc["nSN"];

          if (doc["showHjr"].is<bool>())
            settings.showHijri = doc["showHjr"];

          if (doc["showPsr"].is<bool>())
            settings.showPasaran = doc["showPsr"];

          // 3. Float values
          if (doc["lat"].is<float>())
            settings.latitude = doc["lat"];

          if (doc["long"].is<float>())
            settings.longitude = doc["long"];

          if (doc["is24"].is<bool>())
          {
            settings.is24h = doc["is24"];
          }

          // IMPORTANT: Send response BEFORE potentially heavy EEPROM/Update functions
          request->send(200, "text/plain", "OK");
          delay(10); // Small delay to ensure response is sent before we do heavy lifting

          // Now perform the hardware updates
          saveSettings();
          updateBrightness();
          updateDate();
        }
        else
        {
          request->send(400, "text/plain", "JSON Error");
        }
      });

  server.on("/getsettings", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    JsonDocument doc;
    doc["brtMode"] = settings.brightnessMode;
    doc["dayBrt"] = settings.dayBrightness;
    doc["nightBrt"] = settings.nightBrightness;
    doc["dSH"] = settings.dayStartHour;
    doc["nSN"] = settings.nightStartHour;
    doc["lat"] = settings.latitude;
    doc["long"] = settings.longitude;
    doc["showHjr"] = settings.showHijri;
    doc["showPsr"] = settings.showPasaran;
    doc["hjrOft"] = settings.hijriOffset;
    doc["tmOft"] = settings.timeOffset;
    doc["is24"] = settings.is24h;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output); });

  server.begin();
  showStatusMessage("OK", PA_LEFT);
}
