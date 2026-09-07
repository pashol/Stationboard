#ifndef UTILITIES_H
#define UTILITIES_H

#include <Arduino.h>
#include <NTPClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "globals.h"

// Complete File type for the header-inline persistence core below.
// NOTE: do NOT forward-declare `class File;` here — TFT_eSPI defines
// FS_NO_GLOBALS (smooth-font build), which suppresses FS.h's global
// `using fs::File;`. `fs::File` is always complete after <FS.h>.
#include <FS.h>

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
// Transactional config persistence paths (Task 3: power-loss-safe save).
// Save writes CONFIG_TEMP_PATH, validates it, rotates CONFIG_PATH to
// CONFIG_BACKUP_PATH, then promotes the temp file. Load falls back to the
// backup when the primary is missing or invalid.
constexpr char CONFIG_PATH[] = "/config.json";
constexpr char CONFIG_TEMP_PATH[] = "/config.tmp";
constexpr char CONFIG_BACKUP_PATH[] = "/config.bak";

void logHeap(const char* phase);

// Transactional persistence core (Task 3). Defined inline in this header —
// like URLEncode above — so device tests link it without extra translation
// units; firmware uses the same definitions.

// Parse a config file into `out` via a temp candidate: missing fields fall
// back to the live config, then normalize + validate gate the assignment.
// Returns true iff the file held complete valid JSON describing a valid
// config. Never modifies the global config.
inline bool parseValidateConfigFile(const char* path, Config& out) {
    fs::File configFile = SPIFFS.open(path, FILE_READ);
    if (!configFile) {
        return false;
    }
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        Serial.printf("Config %s: JSON parse error: %s\n", path, error.c_str());
        return false;
    }
    // Parse into a temp copy so a malformed file can never leave
    // the global config half-updated; keep current values on
    // invalid fields via normalize + validate before assigning.
    Config candidate = config;
    candidate.stationId = doc["station_id"] | candidate.stationId;
    candidate.stationId2 = doc["station_id2"] | candidate.stationId2;
    candidate.limit = doc["limit"] | candidate.limit;
    candidate.offset = doc["offset"] | candidate.offset;
    candidate.defaultBrightness = doc["defaultBrightness"] | candidate.defaultBrightness;
    // Night mode settings
    candidate.nightModeEnabled = doc["nightModeEnabled"] | candidate.nightModeEnabled;
    candidate.nightModeStartHour = doc["nightModeStartHour"] | candidate.nightModeStartHour;
    candidate.nightModeStartMinute = doc["nightModeStartMinute"] | candidate.nightModeStartMinute;
    candidate.nightModeEndHour = doc["nightModeEndHour"] | candidate.nightModeEndHour;
    candidate.nightModeEndMinute = doc["nightModeEndMinute"] | candidate.nightModeEndMinute;
    candidate.nightModeWeekendDisable = doc["nightModeWeekendDisable"] | candidate.nightModeWeekendDisable;
    candidate.connectionsEnabled = doc["connectionsEnabled"] | candidate.connectionsEnabled;
    // All-or-nothing gate: a single empty station discards the whole candidate; numerics are clamped by normalize so only empty stations can fail validate here (per-field recovery is Task 4 portal scope).
    normalizeConfiguration(candidate);
    if (!validateConfiguration(candidate)) {
        Serial.printf("Config %s invalid after normalize - keeping previous values\n", path);
        return false;
    }
    out = candidate;
    return true;
}

// Load the primary config, falling back to the backup from the last
// successful transactional save when the primary is missing or invalid.
// /config.tmp is never loaded (it may hold a torn write from a power loss
// during save). Returns true iff a valid config was loaded, else the live
// config (compiled defaults at boot) is left untouched.
inline bool loadConfiguration() {
    Config parsed;
    if (parseValidateConfigFile(CONFIG_PATH, parsed)) {
        config = parsed;
        return true;
    }
    if (parseValidateConfigFile(CONFIG_BACKUP_PATH, parsed)) {
        Serial.println("Config primary invalid - recovered from backup");
        config = parsed;
        return true;
    }
    Serial.println("No valid config found - keeping compiled defaults");
    return false;
}

// Save transactionally: serialize to /config.tmp, verify byte count and
// stream status, re-read + normalize + validate the temp file, rotate the
// current primary to /config.bak, promote the temp file, then re-validate
// the final primary (restoring the backup on failure). Returns true only
// if the final primary is valid.
inline bool saveConfiguration() {
    DynamicJsonDocument doc(1024);
    doc["station_id"] = config.stationId;
    doc["station_id2"] = config.stationId2;
    doc["limit"] = config.limit;
    doc["offset"] = config.offset;
    doc["defaultBrightness"] = config.defaultBrightness;
    // Night mode settings
    doc["nightModeEnabled"] = config.nightModeEnabled;
    doc["nightModeStartHour"] = config.nightModeStartHour;
    doc["nightModeStartMinute"] = config.nightModeStartMinute;
    doc["nightModeEndHour"] = config.nightModeEndHour;
    doc["nightModeEndMinute"] = config.nightModeEndMinute;
    doc["nightModeWeekendDisable"] = config.nightModeWeekendDisable;
    doc["connectionsEnabled"] = config.connectionsEnabled;

    // 1. Serialize to the temp file and verify byte count + stream status.
    fs::File configFile = SPIFFS.open(CONFIG_TEMP_PATH, FILE_WRITE);
    if (!configFile) {
        Serial.println("- failed to open temp file for writing");
        return false;
    }
    size_t bytesWritten = serializeJson(doc, configFile);
    bool streamOk = !configFile.getWriteError();
    configFile.close();
    if (bytesWritten == 0 || !streamOk) {
        Serial.println("- failed to write temp config file");
        SPIFFS.remove(CONFIG_TEMP_PATH);
        return false;
    }

    // 2. Reopen, deserialize, normalize and validate the temp file before
    // it is allowed anywhere near the live primary.
    Config verified;
    if (!parseValidateConfigFile(CONFIG_TEMP_PATH, verified)) {
        Serial.println("- temp config failed validation, discarding");
        SPIFFS.remove(CONFIG_TEMP_PATH);
        return false;
    }

    // 3. Remove any stale backup from a previous cycle.
    if (SPIFFS.exists(CONFIG_BACKUP_PATH)) {
        SPIFFS.remove(CONFIG_BACKUP_PATH);
    }

    // 4. Rotate the current primary aside (only when one exists).
    if (SPIFFS.exists(CONFIG_PATH)) {
        if (!SPIFFS.rename(CONFIG_PATH, CONFIG_BACKUP_PATH)) {
            Serial.println("- failed to back up current config");
            SPIFFS.remove(CONFIG_TEMP_PATH);
            return false;
        }
    }

    // 5. Promote the verified temp file to the primary.
    if (!SPIFFS.rename(CONFIG_TEMP_PATH, CONFIG_PATH)) {
        Serial.println("- failed to promote temp config, restoring backup");
        if (SPIFFS.exists(CONFIG_BACKUP_PATH)) {
            SPIFFS.rename(CONFIG_BACKUP_PATH, CONFIG_PATH);
        }
        return false;
    }

    // 6. Reopen and validate the final primary before reporting success.
    Config finalCheck;
    if (parseValidateConfigFile(CONFIG_PATH, finalCheck)) {
        Serial.println("- config file verified");
        return true;
    }

    // 7. Final validation failed: restore the backup.
    Serial.println("- final config invalid, restoring backup");
    SPIFFS.remove(CONFIG_PATH);
    if (SPIFFS.exists(CONFIG_BACKUP_PATH)) {
        SPIFFS.rename(CONFIG_BACKUP_PATH, CONFIG_PATH);
    }
    return false;
}
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
