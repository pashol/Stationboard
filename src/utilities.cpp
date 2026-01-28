#include "utilities.h"
#include "globals.h"
#include "stationboard.h"
#include "nightmode.h"
#include "networking.h"
#include <WiFiManager.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <TimeLib.h>
#include <Timezone.h>

// Central European Time (Switzerland) with DST rules
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};  // UTC+2 (summer)
TimeChangeRule CET  = {"CET ", Last, Sun, Oct, 3, 60};   // UTC+1 (winter)
Timezone euCET(CEST, CET);

void checkForConfigReset() {
    // Initialize trigger pin as input with pullup
    pinMode(TRIGGER_PIN, INPUT_PULLUP);
    
    // Allow the device to boot properly
    delay(1000);
    
    // Display instruction
    tft.fillScreen(TFT_BLACK);
    tft.loadFont(AA_FONT_SMALL);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Press BOOT to reset", 20, 80);
    tft.drawString("Waiting 3 seconds...", 20, 100);
    
    // Check for button press after boot
    unsigned long startTime = millis();
    while (millis() - startTime < 3000) {  // Wait 3 seconds for button press
        if (digitalRead(TRIGGER_PIN) == LOW) {
            delay(50);  // Simple debounce
            if (digitalRead(TRIGGER_PIN) == LOW) {  // Double check
                Serial.println("Reset button pressed - clearing WiFi settings");
                
                tft.fillScreen(TFT_BLACK);
                tft.drawString("Clearing settings...", 20, 60);
                
                // Create WiFiManager instance
                WiFiManager wm;
                
                // Reset settings
                wm.resetSettings();
                // Then try to mount and clear SPIFFS if possible
                if (SPIFFS.begin(true)) {  // Mount SPIFFS with formatting on failure
                    if (SPIFFS.exists("/config.json")) {
                        SPIFFS.remove("/config.json");
                        Serial.println("Config found and deleted");
                    }
                    SPIFFS.end();  // Clean unmount
                }
                tft.drawString("Settings cleared!", 20, 80);
                tft.drawString("To configure:", 20, 120);
                tft.drawString("Connect to Wifi Stationboard_AP", 20, 140);
                delay(2000);
                
                ESP.restart();
            }
        }
        delay(10);
    }
}

String URLEncode(String msg) {
    const char *hex = "0123456789ABCDEF";
    String encodedMsg = "";
    
    for (char c : msg) {
        if (isAlphaNumeric(c) || c == '-' || c == '_' 
            || c == '.' || c == '~') {
            encodedMsg += c;
        } else {
            encodedMsg += '%';
            encodedMsg += hex[c >> 4];
            encodedMsg += hex[c & 0xF];
        }
    }
    return encodedMsg;
}

String getTimeWithoutSeconds() {
    time_t utc = timeClient.getEpochTime();
    time_t local = euCET.toLocal(utc);

    String hourStr = String(hour(local));
    if (hourStr.length() < 2) hourStr = "0" + hourStr;

    String minuteStr = String(minute(local));
    if (minuteStr.length() < 2) minuteStr = "0" + minuteStr;

    return hourStr + ":" + minuteStr;
}

String getFormattedDateTime() {
    time_t utc = timeClient.getEpochTime();
    time_t local = euCET.toLocal(utc);

    String dateTime = getTimeWithoutSeconds();
    dateTime += " - ";
    dateTime += String(day(local));
    dateTime += ". ";
    dateTime += MONTHS[month(local) - 1];  // month() returns 1-12, MONTHS is 0-indexed

    // Add year
    dateTime += " ";
    dateTime += String(year(local));

    return dateTime;
}

String getDayOfWeek() {
    time_t utc = timeClient.getEpochTime();
    time_t local = euCET.toLocal(utc);
    return DAYS[weekday(local) - 1];  // weekday() returns 1-7 (Sun-Sat), DAYS is 0-indexed
}

void drawCurrentTime() {
    timeClient.update();
    
    // Create temporary sprite for time display
    TFT_eSprite timeSprite(&tft);
    timeSprite.setColorDepth(8);
    timeSprite.createSprite(tft.width() / 2, 25);
    timeSprite.loadFont(AA_FONT_SMALL);
    
    // Clear sprite and set background
    timeSprite.fillSprite(TFT_WHITE);
    
    // Draw time and date
    timeSprite.setTextColor(TFT_BLACK, TFT_WHITE);
    timeSprite.drawString(getFormattedDateTime(), 4, 5);
    
    // Push to bottom of screen
    timeSprite.pushSprite(0, tft.height() - 25);
}

String getFormattedTimeRelativeToNow(int minutesOffset) {
    time_t utc = timeClient.getEpochTime() + (minutesOffset * 60);
    time_t local = euCET.toLocal(utc);

    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d",
             year(local),
             month(local),
             day(local),
             hour(local),
             minute(local));
    return String(buffer);
}

void updateBrightness() {
    // Set backlight brightness using LED PWM channel
    ledcWrite(PWM_CHANNEL, BRIGHTNESS_LEVELS[currentBrightnessIndex]);
    
    // For debugging, output current level to serial (can also use display)
    Serial.printf("Brightness level: %d\n", BRIGHTNESS_LEVELS[currentBrightnessIndex]);
}

void cycleBrightness() {
    if (inNightMode) {
        // During night mode, button click triggers temporary wake
        handleNightModeButton();
    } else {
        // Normal brightness cycling
        currentBrightnessIndex = (currentBrightnessIndex + 1) % NUM_LEVELS;
        updateBrightness();
    }
}

void debugInfo() {
    Serial.println("Debug info:");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Largest block: %d bytes\n", ESP.getMaxAllocHeap());
    UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
    Serial.printf("Stack watermark: %d bytes\n", watermark);
    Serial.println("RSSI: " + String(WiFi.RSSI()) + "dBm");
    Serial.println("WiFi:" + String(WiFi.status()));
    Serial.println("SSID:" + WiFi.SSID());
    Serial.println("IP:" + WiFi.localIP().toString());
    Serial.println("MAC:" + WiFi.macAddress());
    Serial.println("CPU:" + String(getCpuFrequencyMhz()) + "MHz");
    Serial.println("BL:" + String(ledcRead(PWM_CHANNEL)));
    //Serial.println("VDD:" + String(readVDD()) + "mV");
    Serial.println("Uptime:" + String(millis() / 1000) + "s");
}

void loadConfiguration() {
    if (SPIFFS.exists("/config.json")) {
        File configFile = SPIFFS.open("/config.json", FILE_READ);
        if (configFile) {
            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, configFile);
            
            if (!error) {
                config.stationId = doc["station_id"].as<String>();
                config.stationId2 = doc["station_id2"].as<String>();
                config.limit = doc["limit"].as<int>();
                config.offset = doc["offset"].as<int>();
                config.defaultBrightness = doc["defaultBrightness"].as<int>();
                // Night mode settings
                config.nightModeEnabled = doc["nightModeEnabled"] | false;
                config.nightModeStartHour = doc["nightModeStartHour"] | 22;
                config.nightModeStartMinute = doc["nightModeStartMinute"] | 0;
                config.nightModeEndHour = doc["nightModeEndHour"] | 7;
                config.nightModeEndMinute = doc["nightModeEndMinute"] | 0;
                config.nightModeWeekendDisable = doc["nightModeWeekendDisable"] | false;
            }
            configFile.close();
        } else {
            Serial.println("No config found");
        }
    }
}

void saveConfiguration() {
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

    File configFile = SPIFFS.open("/config.json", FILE_WRITE);
    if (!configFile) {
        Serial.println("- failed to open file for writing");
        return;
    }

    serializeJson(doc, Serial);
    serializeJson(doc, configFile);
    configFile.close();
    
    // Verify the write
    configFile = SPIFFS.open("/config.json");
    if (!configFile) {
        Serial.println("- failed to open file for verification");
        return;
    }
    
    if (configFile.size() > 0) {
        Serial.println("- config file verified");
        Serial.println("Contents:");
        while(configFile.available()) {
            Serial.write(configFile.read());
        }
    } else {
        Serial.println("- config file appears empty");
    }
    configFile.close();
}

void saveConfigCallback() {
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

void drawPortalIndicator() {
    // Save current text datum
    uint8_t oldDatum = tft.getTextDatum();

    // Clear blue area only (leave white footer intact)
    tft.fillRect(0, 0, tft.width(), tft.height() - 25, TFT_BLUE);

    tft.loadFont(AA_FONT_SMALL);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setTextDatum(MC_DATUM);  // Middle center alignment

    // Center of blue area
    int centerY = (tft.height() - 25) / 2;

    tft.drawString("CONFIG PORTAL ACTIVE", tft.width() / 2, centerY - 20);
    tft.drawString("http://" + WiFi.localIP().toString(), tft.width() / 2, centerY);
    tft.drawString("Triple-click to stop", tft.width() / 2, centerY + 20);

    // Restore text datum
    tft.setTextDatum(oldDatum);
}

void startConfigPortal() {
    // Config portal is disabled during night mode (when display is dark)
    if (inNightMode && !temporaryNightWake) {
        Serial.println("Config portal disabled during night mode");
        return;
    }

    // Extend temporary wake if in night mode
    if (inNightMode && temporaryNightWake) {
        nightWakeStartTime = millis();
        Serial.println("Extended night wake");
    }

    int numClicks = button.getNumberClicks();
    if (numClicks == 3) {  // Triple click
        Serial.println("Triple click detected");
        if(!portalRunning){
            Serial.println("Starting Portal");
            Serial.printf("Config portal started at: http://%s\n", WiFi.localIP().toString().c_str());
            wm.startWebPortal();
            portalRunning = true;
            drawPortalIndicator();
          }
          else{
            Serial.println("Stopping Portal");
            wm.stopWebPortal();
            portalRunning = false;
            // Restore normal display
            tft.fillScreen(TFT_BLUE);
            tft.fillRect(0, tft.height() - 25 , tft.width(), 25, TFT_WHITE);
            drawCurrentTime();
            drawStationboard();
            drawBTC();
          }
    }
}

void switchStation() {
    if (inNightMode && !temporaryNightWake) {
        // During night mode without temporary wake, do nothing
        return;
    }
    
    // Extend temporary wake if in night mode
    if (inNightMode && temporaryNightWake) {
        nightWakeStartTime = millis();
        Serial.println("Extended night wake");
    }
    
    isFirstStation = !isFirstStation;
    Serial.println(isFirstStation ? "Switched to first station" : "Switched to second station");
    drawStationboard(); // Redraw the stationboard with the new station, force refresh
}

void displayStatus(bool isSuccess) {
    // Clear the status area with white background
    tft.fillRect(tft.width() - 25, tft.height() - 25, 25, 25, TFT_WHITE);
    
    // Draw the circle in green or red based on status
    tft.fillCircle(tft.width() - 13, tft.height() - 13, 3, 
                   isSuccess ? TFT_GREEN : TFT_RED);
}

// Night mode helper functions
bool isWeekend() {
    time_t utc = timeClient.getEpochTime();
    time_t local = euCET.toLocal(utc);
    int dayOfWeek = weekday(local); // 1 = Sunday, 7 = Saturday
    return (dayOfWeek == 1 || dayOfWeek == 7);
}

bool isNightModeActive() {
    if (!config.nightModeEnabled) return false;
    
    // Check if weekend should disable night mode
    if (config.nightModeWeekendDisable && isWeekend()) {
        return false;
    }
    
    time_t utc = timeClient.getEpochTime();
    time_t local = euCET.toLocal(utc);
    int currentHour = hour(local);
    int currentMinute = minute(local);
    int currentTime = currentHour * 60 + currentMinute;
    
    int startTime = config.nightModeStartHour * 60 + config.nightModeStartMinute;
    int endTime = config.nightModeEndHour * 60 + config.nightModeEndMinute;
    
    if (startTime < endTime) {
        // Normal case: e.g., 22:00 to 23:00 (same day)
        return (currentTime >= startTime && currentTime < endTime);
    } else {
        // Crosses midnight: e.g., 22:00 to 07:00
        return (currentTime >= startTime || currentTime < endTime);
    }
}

void enterNightMode() {
    if (inNightMode) return;
    
    Serial.println("Entering night mode");
    inNightMode = true;
    temporaryNightWake = false;
    
    // Turn off display
    ledcWrite(PWM_CHANNEL, 0);
    
    // Clear screen to black
    tft.fillScreen(TFT_BLACK);
}

void exitNightMode() {
    if (!inNightMode) return;
    
    Serial.println("Exiting night mode");
    inNightMode = false;
    temporaryNightWake = false;
    
    // Restore brightness
    updateBrightness();
    
    // Redraw screen
    tft.fillScreen(TFT_BLUE);
    tft.fillRect(0, tft.height() - 25 , tft.width(), 25, TFT_WHITE);
}

void handleNightModeButton() {
    if (!inNightMode) return;
    
    // Wake up temporarily
    temporaryNightWake = true;
    nightWakeStartTime = millis();
    
    // Restore brightness temporarily
    updateBrightness();
    
    // Redraw screen
    tft.fillScreen(TFT_BLUE);
    tft.fillRect(0, tft.height() - 25 , tft.width(), 25, TFT_WHITE);
    
    Serial.println("Temporary night wake activated");
}

void updateNightModeDisplay() {
    if (!inNightMode || !temporaryNightWake) return;
    
    // Check if temporary wake duration has expired
    if (millis() - nightWakeStartTime >= NIGHT_WAKE_DURATION) {
        temporaryNightWake = false;
        
        // Turn off display again
        ledcWrite(PWM_CHANNEL, 0);
        tft.fillScreen(TFT_BLACK);
        
        Serial.println("Temporary night wake ended");
    }
}

void checkNightMode() {
    bool shouldBeInNightMode = isNightModeActive();
    
    if (shouldBeInNightMode && !inNightMode) {
        enterNightMode();
    } else if (!shouldBeInNightMode && inNightMode) {
        exitNightMode();
    }
}
