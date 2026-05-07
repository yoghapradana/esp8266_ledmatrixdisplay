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
#define CLOCK_DISPLAY_TIMEOUT 10000  // Show clock for 10 seconds after last interaction
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
  int32_t timeOffset = 25200;   // GMT+7 default (seconds)
  float latitude = -7.2575;     // Surabaya default
  float longitude = 112.7521;   // Surabaya default

  uint8_t dayBrightness = 8;
  uint8_t nightBrightness = 1;
  uint16_t dayStartTime = 360;  // day start time in minutes since midnight (default 6:00)
  uint16_t nightStartTime = 1080; // night start time in minutes since midnight (default 18:00)
  int8_t hijriOffset = 0; // User-defined offset for Hijri date (in days)

  bool autoBrightness = false;  // "false=manual" or "true=auto"
  bool showHijri = true;
  bool showPasaran = true;
  bool is24h = true; // 24-hour format by default
};

struct Credentials {
  char ssid[32];  // 31 characters + null terminator
  char password[64];  // 63 characters + null terminator
};