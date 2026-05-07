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

// AsyncWebServer on Port 80
AsyncWebServer server(80);

WiFiUDP ntpUDP;
// NTP Client with 3-hour update interval (in milliseconds)
NTPClient timeClient(ntpUDP, NTP_SERVER, 0, 3 * 60 * 60 * 1000); // Initial offset is UTC, will be updated from settings
DisplaySettings settings;										 // Global settings struct
Credentials credentials;										 // Global credentials struct

// Variables
char timeStr[8] = "00:00"; // HH:MM format with blinking colon + extra char for a/p indicator + null terminator
char dateStr[120] = "";	   // For full date text
// Brightness change times
// [0] = day start time in minutes, [1] = night start time in minutes
uint16_t brightnessTime[2];
String customMessage = "";
uint8_t customMessageRotations = 0;
uint8_t currentRotation = 0;
unsigned long lastTick = 0;
unsigned long lastDateDisplay = 0;
unsigned long lastHijriUpdate = 0;
uint8_t dotAnimationPos = 0;
const char *dotAnimation[] = {".   ", "..  ", "... ", "...."}; // Dot animation frames
unsigned long timeOut = 0;

//-----Flags------//
// OTA Update Status
bool otaEnabled = false;
bool otaActive = false;

bool rebootReq = false;
bool colonVisible = true;
bool showingDate = false;
bool showingCustomMessage = false;
bool reqHijriFetch = false;
bool reqSaveSettings = false;
bool initialFetchDone = false;
bool ntpSyncSuccess = false;

// Islamic date variable
// [0] = day, [1] = month, [2] = year
int hijridates[3];

// ========================================== //
// ===== FORWARD DECLARATIONS (PROTOTYPES) == //
// ========================================== //
void showStatusMessage(const char *msg, textPosition_t alignment = PA_LEFT);
void loadSettings();
void saveSettings();
uint16_t calculateSunEvent(float lat, float lon, int dayOfYear, bool isSunrise, int timeOffset);
void updateBrightness(uint16_t dayStartTime, uint16_t nightStartTime);
void loadCredentials();
void saveCredentials();
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

	if (settings.autoBrightness)
	{
		brightnessTime[0] = calculateSunEvent(settings.latitude, settings.longitude, day() + (month() - 1) * 30, true, settings.timeOffset);
		brightnessTime[1] = calculateSunEvent(settings.latitude, settings.longitude, day() + (month() - 1) * 30, false, settings.timeOffset);
	}
	brightnessTime[0] = settings.dayStartTime;
	brightnessTime[1] = settings.nightStartTime;
	updateBrightness(settings.dayStartTime, settings.nightStartTime);
	loadCredentials();

	WiFi.begin(credentials.ssid, credentials.password);
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

	// Initial NTP Sync
	int retryCount = 0;
	int maxRetries = 15; // Try for 15 seconds
	showStatusMessage("NTP", PA_LEFT);
	timeClient.begin();
	// retries every second until successful, bypassing the normal update interval
	// to avoid getting zero epoch

	while (retryCount < maxRetries)
	{
		if (timeClient.forceUpdate())
		{
			ntpSyncSuccess = true;
			break; // Exit loop early if we get the time!
		}
		retryCount++;
		delay(1000);
	}
	// TimeLib Sync with NTP Client
	setTime(timeClient.getEpochTime());

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

	if (!ntpSyncSuccess)
	{
		if (display.displayAnimate())
		{
			display.displayScroll("NTP SYNC Failed, Check Network", PA_LEFT, PA_SCROLL_LEFT, SCROLL_SPEED);
		}
		return; // Don't proceed until we have the time, otherwise date-based features won't work
	}

	// Update time and date from NTP
	// actual update happens if timelimit is reached
	updateTime();

	// 1. Trigger at Midnight
	bool isMidnight = (timeClient.getHours() == 0 && timeClient.getMinutes() == 0 && timeClient.getSeconds() == 0);

	// 2. Trigger on Reset (only once after NTP is valid)
	bool isInitialSync = (!initialFetchDone && timeClient.isTimeSet());

	// --- THE HIJRI FETCH BLOCK ---
	if (isMidnight || isInitialSync || reqHijriFetch)
	{
		static int lastFetchedDay = -1;		  // Guard variable
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

	if (reqSaveSettings)
	{
		yield(); // Yield to ensure we don't block other operations
		saveSettings();

		if (settings.autoBrightness)
		{
			brightnessTime[0] = calculateSunEvent(settings.latitude, settings.longitude, day() + (month() - 1) * 30, true, settings.timeOffset);
			brightnessTime[1] = calculateSunEvent(settings.latitude, settings.longitude, day() + (month() - 1) * 30, false, settings.timeOffset);
		}
		brightnessTime[0] = settings.dayStartTime;
		brightnessTime[1] = settings.nightStartTime;
		updateBrightness(settings.dayStartTime, settings.nightStartTime);

		updateDate();
		reqSaveSettings = false;
		yield(); // Yield again after updates
	}

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

		static uint8_t lastMinute;
		if (minute() != lastMinute)
		{
			lastMinute = minute();
			updateBrightness(settings.dayStartTime, settings.nightStartTime);
		}
	}

	if (millis() - lastDateDisplay >= CLOCK_DISPLAY_TIMEOUT)
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

void loadCredentials()
{
	EEPROM.get(0, credentials);
	showStatusMessage("OK", PA_LEFT);
}

void saveCredentials()
{
	EEPROM.put(0, credentials);
	EEPROM.commit();
	showStatusMessage("OK", PA_LEFT);
}

// Returns time of sunrise or sunset for given day of year in minutes since midnight (0-1440)
uint16_t calculateSunEvent(float lat, float lon, int dayOfYear, bool isSunrise, int timeOffset)
{

	float declination = -23.45 * cos(2 * PI * (dayOfYear + 10) / 365.0);
	// Calculate the target value for acos
	float acosTarget = -tan(lat * PI / 180.0) * tan(declination * PI / 180.0);
	float hourAngle;

	// EXTREME CASE PROTECTION:
	if (acosTarget >= 1.0)
	{
		// Value > 1 means the sun never rises (Polar Night)
		// We force the hourAngle to 0, meaning Sunrise and Sunset both equal Solar Noon
		hourAngle = 0.0;
	}
	else if (acosTarget <= -1.0)
	{
		// Value < -1 means the sun never sets (Midnight Sun)
		// We force the hourAngle to 180 degrees (12 hours from noon, i.e., 24hr day)
		hourAngle = 180.0;
	}
	else
	{
		// Normal operation
		hourAngle = acos(acosTarget) * 180.0 / PI;
	}

	float solarNoon = 12.0 + (timeOffset / 3600.0) - (lon / 15.0);
	float eventTimeInHours;
	if (isSunrise)
	{
		eventTimeInHours = solarNoon - (hourAngle / 15.0);
	}
	else
	{
		eventTimeInHours = solarNoon + (hourAngle / 15.0);
	}

	// Convert to minutes since midnight
	int32_t minutesSinceMidnight = round(eventTimeInHours * 60.0);

	// Safe signed math guard
	if (minutesSinceMidnight < 0)
	{
		minutesSinceMidnight += 1440;
	}

	// Safe to cast to uint16_t now that the value is guaranteed to be between 0 and 1439
	return (uint16_t)(minutesSinceMidnight % 1440);
}

void updateBrightness(uint16_t dayStartTime, uint16_t nightStartTime)
{
	uint8_t targetBrightness;
	uint16_t currentTime = (hour() * 60) + minute();

	// Check if current time matches day or night start time

	if (dayStartTime < nightStartTime)
	{
		// Normal case: Day starts and ends within the same 24-hour period
		if (currentTime >= dayStartTime && currentTime < nightStartTime)
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
		// Edge case: Day starts before midnight and ends after midnight
		if (currentTime >= dayStartTime || currentTime < nightStartTime)
		{
			targetBrightness = settings.dayBrightness;
		}
		else
		{
			targetBrightness = settings.nightBrightness;
		}
	}
	display.setIntensity(targetBrightness);
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
		snprintf(timeStr, sizeof(timeStr), "%2d%c%02d%c",
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
	unsigned long daysSinceEpoch = now() / 86400;

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
}

void updateDate()
{
	updateTime(); // Ensure time is updated before calculating date
	unsigned long daysSinceEpoch = now() / 86400;

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
	time_t current_time = now();					// Get current time in seconds since epoch
	current_time += (settings.hijriOffset * 86400); // Add/Subtract days in seconds

	struct tm *t = localtime(&current_time);

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
    delay(2000); 
	ESP.restart(); });

	ArduinoOTA.begin();
}

String htmlProcessor(const String &var)
{
	if (var == "CURRENT_SSID")
	{
		// Check if the first character is null (empty)
		return (credentials.ssid[0] != '\0') ? String(credentials.ssid) : "Not set";
	}

	if (var == "WIFI_STATUS")
	{
		if (WiFi.status() == WL_CONNECTED)
		{
			return "Connected (" + WiFi.localIP().toString() + ")";
		}
		else if (credentials.ssid[0] != '\0')
		{
			return "Saved but not connected";
		}
		else
		{
			return "No saved network";
		}
	}

	if (var == "TIME_OFFSET")
		return String(settings.timeOffset);

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

	server.on("/saveCredentials", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
			  {
        JsonDocument doc;
        // Using 'total' ensures we consider the expected length of the body
        DeserializationError error = deserializeJson(doc, data, len);

        if (!error) {
        // Use strlcpy for safer copying than strncpy (it handles null-termination automatically)
        strlcpy(credentials.ssid, doc["ssid"] | "", sizeof(credentials.ssid));
        strlcpy(credentials.password, doc["password"] | "", sizeof(credentials.password));

        saveCredentials();
        request->send(200, "text/plain", "OK");
        rebootReq = true;
    } else {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
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

	server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
			  {
        JsonDocument doc;
        // Using 'total' ensures we consider the expected length of the body
        DeserializationError error = deserializeJson(doc, data, len);

        if (!error)
        {
          // 1. Brightness Mode (Boolean)
          if (doc["brtMode"].is<bool>())
          {
            settings.autoBrightness = doc["brtMode"];
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
              //update offset if new value received and trigger API fetch request
              settings.hijriOffset = newHijriOffset;
              reqHijriFetch = true; // Trigger a fetch with the new offset
            }
          }
          if (doc["dBrt"].is<int>())
            settings.dayBrightness = doc["dBrt"];

          if (doc["ntBrt"].is<int>())
            settings.nightBrightness = doc["ntBrt"];

          if (doc["dST"].is<int>())
            settings.dayStartTime = doc["dST"];

          if (doc["nST"].is<int>())
            settings.nightStartTime = doc["nST"];

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
          reqSaveSettings = true; // Flag to indicate we need to save settings after response
          
        }
        else
        {
          request->send(400, "text/plain", "JSON Error");
        } });

	server.on("/getsettings", HTTP_GET, [](AsyncWebServerRequest *request)
			  {
    JsonDocument doc;
    doc["brtMode"] = settings.autoBrightness;
    doc["dBrt"] = settings.dayBrightness;
    doc["ntBrt"] = settings.nightBrightness;
    doc["dSH"] = settings.dayStartTime / 60; // Convert back to hours for response
    doc["dSM"] = settings.dayStartTime % 60; // Convert back to minutes for response
    doc["nSH"] = settings.nightStartTime / 60; // Convert back to hours for response
    doc["nSM"] = settings.nightStartTime % 60; // Convert back to minutes for response
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
