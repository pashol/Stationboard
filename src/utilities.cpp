#include "utilities.h"
#include "globals.h"
#include "stationboard.h"
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
    // Normal brightness cycling
    currentBrightnessIndex = (currentBrightnessIndex + 1) % NUM_LEVELS;
    updateBrightness();
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

void startConfigPortal() {
    int numClicks = button.getNumberClicks();
    if (numClicks == 3) {  // Triple click
        Serial.println("Triple click detected");
        if(!portalRunning){
            Serial.println("Starting Portal");
            Serial.printf("Config portal started at: http://%s\n", WiFi.localIP().toString().c_str());
            wm.startWebPortal();
            portalRunning = true;
          }
          else{
            Serial.println("Stopping Portal");
            wm.stopWebPortal();
            portalRunning = false;
          }
    }
}

void switchStation() {
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
