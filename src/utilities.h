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
// URL-encodes a string byte-wise. Iterates unsigned bytes so UTF-8
// multibyte sequences encode correctly (a signed char would sign-extend
// and index the hex table out of bounds). Defined inline here so device
// tests link it without extra translation units.
inline String URLEncode(String msg) {
    const char *hex = "0123456789ABCDEF";
    String encodedMsg = "";

    for (unsigned char c : msg) {
        if (isAlphaNumeric(c) || c == '-' || c == '_'
            || c == '.' || c == '~') {
            encodedMsg += static_cast<char>(c);
        } else {
            encodedMsg += '%';
            encodedMsg += hex[(c >> 4) & 0x0F];
            encodedMsg += hex[c & 0x0F];
        }
    }
    return encodedMsg;
}
// Single validation path shared by flash load and portal ingress (Task 4).
struct Config;
inline bool validateConfiguration(const Config& candidate);
inline void normalizeConfiguration(Config& candidate);
String getTimeWithoutSeconds();
String getFormattedDateTime();
String getDayOfWeek();
void drawCurrentTime();
String getFormattedTimeRelativeToNow(int minutesOffset);
void updateBrightness();
void cycleBrightness();
void debugInfo();
void logHeap(const char* phase);
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
