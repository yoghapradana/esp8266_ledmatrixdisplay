#pragma once

#include <Arduino.h>
#include <MD_MAX72xx.h> // Required for the HARDWARE_TYPE definition

// ========================================== //
// ===== HARDWARE & PIN CONFIGURATION ======= //
// ========================================== //
#define DATA_PIN 3  // RX pin
#define CS_PIN 0    // GPIO0
#define CLK_PIN 2   // GPIO2

// ========================================== //
// ===== DISPLAY & ANIMATION TIMINGS ======== //
// ========================================== //
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define SCROLL_SPEED 20
#define DATE_DISPLAY_INTERVAL 10000  // Show date every 10 seconds
#define TICK_INTERVAL 500            // Colon animation interval in ms

// ========================================== //
// ===== NETWORK & SECURITY ================= //
// ========================================== //
#define AP_MODE_TIMEOUT 300000       // AP mode timeout in ms (5 minutes)
#define AP_SSID "LED_Display-Config" // Default AP Mode Network Name
#define AP_PASSWORD "12345678"       // Default AP Mode Password (Min 8 chars)

#define OTA_HOSTNAME "esp8266-dotmatrixdisp"
#define OTA_PASSWORD "admin"

// ========================================== //
// ===== EXTERNAL SERVICES & APIs =========== //
// ========================================== //
#define NTP_SERVER "time.nist.gov"
#define HIJRI_API_URL "http://api.aladhan.com/v1/gToH"

// ========================================== //
// ===== DEFAULT EEPROM SETTINGS ============ //
// ========================================== //
// We define the structure here, so the main file knows what it looks like
struct DisplaySettings {
  bool brightnessMode = false;  // "false=manual" or "true=auto"
  uint8_t dayBrightness = 8;
  uint8_t nightBrightness = 1;
  uint8_t dayStartHour = 6;     // Hour when day mode starts
  uint8_t nightStartHour = 18;  // Hour when night mode starts
  float latitude = -7.2575;     // Surabaya default
  float longitude = 112.7521;   // Surabaya default
  bool showHijri = true;
  bool showPasaran = true;
  int32_t timeOffset = 25200;   // GMT+7 default (seconds)

  int8_t hijriOffset = 0; // User-defined offset for Hijri date (in days)
  bool is24h = true; // 24-hour format by default
};