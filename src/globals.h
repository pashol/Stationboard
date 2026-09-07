#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <OneButton.h>
#include <NTPClient.h>
#include "NotoSansBold15.h"

// Config parameters from the WIFIManager Setup
struct Config {
    String stationId = "Luzern";
    String stationId2 = "Zug";  // Second station
    int limit = 8;
    int offset = 0;
    int defaultBrightness = 4;
    // Night mode settings
    bool nightModeEnabled = false;
    int nightModeStartHour = 22;
    int nightModeStartMinute = 0;
    int nightModeEndHour = 7;
    int nightModeEndMinute = 0;
    bool nightModeWeekendDisable = false;
    bool connectionsEnabled = false;
};

extern Config config;
// 0 = Station 1, 1 = Station 2, 2 = Connections
extern int displayMode;
extern TFT_eSPI tft;
extern WiFiManager wm;
extern bool shouldSaveConfig;
extern const char* AA_FONT_SMALL;
extern int numClicks;
extern bool portalRunning;

// Constants
extern const long timeOffset;
extern const unsigned long HTTP_TIMEOUT;
extern const char* getBTCAPI;

// Position constants
#define POS_TIME 53
#define POS_DELAY 97
#define POS_BUS  3
#define POS_TO   130
#define POS_INC  18
#define POS_FIRST 32

// Font definition
#define AA_FONT_SMALL NotoSansBold15

// Button and brightness constants
#define TRIGGER_PIN 0
extern const int BUTTON_PIN;
extern const int BRIGHTNESS_LEVELS[];
extern const int NUM_LEVELS;
extern int currentBrightnessIndex;



// PWM constants
extern const int PWM_CHANNEL;
extern const int PWM_FREQ;
extern const int PWM_RESOLUTION;
extern const int BACKLIGHT_PIN;

// Time management
extern unsigned long temporaryOnStart;
extern const unsigned long TEMP_ON_DURATION;

// Night mode state
struct NightModeState {
    bool active = false;
    bool temporaryWake = false;
    unsigned long wakeStartTime = 0;
};
extern NightModeState nightMode;
extern const unsigned long NIGHT_WAKE_DURATION;
extern const unsigned long NIGHT_CHECK_INTERVAL;
extern bool forceRefresh;

// Loop refresh cycle
extern unsigned long previousMillis;
extern const unsigned long SLEEP_DURATION;
extern const unsigned long UPDATE_INTERVAL;
extern const unsigned long UPDATE_DURATION;

// Stability limits (Task 1 baseline: fixed capacities for later bounded work)
constexpr size_t MAX_TRANSPORTS = 10;
constexpr size_t MAX_CONNECTIONS = 8;
constexpr size_t MAX_API_RESPONSE_BYTES = 32768;
constexpr size_t STATIONBOARD_JSON_CAPACITY = 8192;
constexpr size_t CONNECTIONS_JSON_CAPACITY = 8192;

// Configuration bounds (Task 2: single validation path for flash + portal ingress)
// MAX_STATION_LENGTH matches the WiFiManager portal field length for station IDs.
constexpr size_t MAX_STATION_LENGTH = 150;
// Maximum absolute stationboard time offset in minutes (API `offset` parameter).
constexpr int MAX_STATION_OFFSET_MINUTES = 120;

// Single validation path shared by flash load and portal ingress (Task 4).
// Defined inline here (header-only, like the Task 1 limits) so device tests
// link without extra translation units; firmware uses the same definitions.
// validateConfiguration rejects empty station IDs and out-of-range numerics.
// normalizeConfiguration constrains every numeric into range and disables a
// zero-length night schedule (start == end) instead of entering it.
inline bool validateConfiguration(const Config& candidate) {
    if (candidate.stationId.length() == 0) return false;
    if (candidate.stationId2.length() == 0) return false;
    if (candidate.stationId.length() > MAX_STATION_LENGTH) return false;
    if (candidate.stationId2.length() > MAX_STATION_LENGTH) return false;
    if (candidate.limit < 1 || candidate.limit > 10) return false;
    if (candidate.offset < -MAX_STATION_OFFSET_MINUTES ||
        candidate.offset > MAX_STATION_OFFSET_MINUTES) return false;
    if (candidate.defaultBrightness < 0 || candidate.defaultBrightness > 4) return false;
    if (candidate.nightModeStartHour < 0 || candidate.nightModeStartHour > 23) return false;
    if (candidate.nightModeStartMinute < 0 || candidate.nightModeStartMinute > 59) return false;
    if (candidate.nightModeEndHour < 0 || candidate.nightModeEndHour > 23) return false;
    if (candidate.nightModeEndMinute < 0 || candidate.nightModeEndMinute > 59) return false;
    return true;
}

inline void normalizeConfiguration(Config& candidate) {
    candidate.limit = constrain(candidate.limit, 1, 10);
    candidate.defaultBrightness = constrain(candidate.defaultBrightness, 0, 4);
    candidate.offset = constrain(candidate.offset,
                                 -MAX_STATION_OFFSET_MINUTES,
                                 MAX_STATION_OFFSET_MINUTES);
    candidate.nightModeStartHour = constrain(candidate.nightModeStartHour, 0, 23);
    candidate.nightModeStartMinute = constrain(candidate.nightModeStartMinute, 0, 59);
    candidate.nightModeEndHour = constrain(candidate.nightModeEndHour, 0, 23);
    candidate.nightModeEndMinute = constrain(candidate.nightModeEndMinute, 0, 59);
    if (candidate.stationId.length() > MAX_STATION_LENGTH) {
        candidate.stationId = candidate.stationId.substring(0, MAX_STATION_LENGTH);
    }
    if (candidate.stationId2.length() > MAX_STATION_LENGTH) {
        candidate.stationId2 = candidate.stationId2.substring(0, MAX_STATION_LENGTH);
    }
    if (candidate.nightModeEnabled &&
        candidate.nightModeStartHour == candidate.nightModeEndHour &&
        candidate.nightModeStartMinute == candidate.nightModeEndMinute) {
        candidate.nightModeEnabled = false;
    }
}

// Objects
extern OneButton button;
extern NTPClient timeClient;
extern WiFiUDP ntpUDP;

// Function declarations
void displayStatus(bool isSuccess);
void lightSleep();
void checkForConfigReset();
void loadConfiguration();
void saveConfiguration();
void saveConfigCallback();
void switchStation(); // New function to switch between stations

#include <ArduinoJson.h>

// Firmware version
#define FIRMWARE_VERSION "1.3.0"

// Additional constants
extern const char* DAYS[];
extern const char* MONTHS[];

#endif // GLOBALS_H
