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
};

extern Config config;
extern bool isFirstStation; // Track which station is currently displayed
extern TFT_eSPI tft;
extern WiFiManager wm;
extern bool shouldSaveConfig;
extern const char* AA_FONT_SMALL;

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

// Loop refresh cycle
extern unsigned long previousMillis;
extern const unsigned long UPDATE_INTERVAL;

// Objects
extern OneButton button;
extern NTPClient timeClient;
extern WiFiUDP ntpUDP;

// Function declarations
void displayStatus(bool isSuccess);
void checkForConfigReset();
void loadConfiguration();
void saveConfiguration();
void saveConfigCallback();
void switchStation(); // New function to switch between stations

#include <ArduinoJson.h>

// Additional constants
extern const char* DAYS[];
extern const char* MONTHS[];

#endif // GLOBALS_H
