#ifndef UTILITIES_H
#define UTILITIES_H

#include <Arduino.h>
#include <NTPClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Forward declaration of File class
class File;

extern NTPClient timeClient;

void checkForConfigReset();
String URLEncode(String msg);
String getTimeWithoutSeconds();
String getFormattedDateTime();
String getDayOfWeek();
void drawCurrentTime();
String getFormattedTimeRelativeToNow(int minutesOffset);
void updateBrightness();
void cycleBrightness();
void debugInfo();
void loadConfiguration();
void saveConfiguration();
void saveConfigCallback();
void displayStatus(bool isSuccess);
void startConfigPortal();
void drawPortalIndicator();
void switchStation();

// Night mode functions
bool isNightModeActive();
bool isWeekend();
void checkNightMode();
void enterNightMode();
void exitNightMode();
void handleNightModeButton();
void updateNightModeDisplay();

// Constants
extern const char* DAYS[];
extern const char* MONTHS[];
extern const int BRIGHTNESS_LEVELS[];
extern const int NUM_LEVELS;
extern int currentBrightnessIndex;
extern const int PWM_CHANNEL;

// Global variables
extern int numClicks;
extern bool portalRunning;

// ArduinoJson forward declarations
using ArduinoJson::DynamicJsonDocument;
using ArduinoJson::DeserializationError;

#endif // UTILITIES_H
