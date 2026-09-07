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
