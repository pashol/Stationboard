#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include "globals.h"
#include "networking.h"
#include "utilities.h"
#include "stationboard.h"
#include "ota.h"

void setup() {
    Serial.begin(115200);

    if (SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mounted");
    }

    // Initialize display
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);

    // Initialize PWM for backlight
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(BACKLIGHT_PIN, PWM_CHANNEL);
    updateBrightness(); //initial value

    // Set up button callbacks
    button.attachClick(cycleBrightness);
    button.setPressMs(10000); // 10 seconds for long press
    button.attachLongPressStart(handleLongPress);

    // Call WiFiManager setup
    setupWiFiManager();

    // Set custom brightness
    currentBrightnessIndex = config.defaultBrightness; //setting initial brightness from setup
    updateBrightness();
         
    // Initialize time client
    timeClient.begin();
    timeClient.setUpdateInterval(900000); // Update every 15 minutes (900000ms)
    timeClient.setTimeOffset(timeOffset); // UTC+1 (Sowieso nur Schwiiz)

    // Initial Screen Setup
    tft.fillScreen(TFT_BLUE);
    tft.fillRect(0, tft.height() - 25 , tft.width(), 25, TFT_WHITE); //footer
    tft.loadFont(AA_FONT_SMALL);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);

    // Initial data fetch
    if (WiFi.status() == WL_CONNECTED) {
        drawCurrentTime();
        drawStationboard();
        drawBTC();
    }

    debugInfo();
}

void loop() {
    button.tick();

    handleOTA();

    if (!otaMode) {
        unsigned long currentMillis = millis();
        
        if (currentMillis - previousMillis >= UPDATE_INTERVAL) {
            previousMillis = currentMillis;
            
            if (WiFi.status() == WL_CONNECTED) {
                drawCurrentTime();
                drawStationboard();
                drawBTC();
            } else {
                tft.setTextColor(TFT_RED, TFT_BLUE);
                tft.drawString("WiFi disconnected", POS_BUS, POS_FIRST);
                Serial.println("Wifi disconnected");
                WiFi.reconnect();
            }

            debugInfo();
        }
    }
}
