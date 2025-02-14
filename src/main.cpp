#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include "globals.h"
#include "networking.h"
#include "utilities.h"
#include "stationboard.h"
#include "ota.h"

#define BUTTON_SLEEP GPIO_NUM_0  // Boot Button

void lightSleep() {
    Serial.println("Preparing for light sleep...");
    
    // Store current backlight state
    int currentBrightness = ledcRead(PWM_CHANNEL);

    if (currentBrightness > 192 ) {
        // Configure wake-up sources
        esp_sleep_enable_timer_wakeup(SLEEP_DURATION);
        esp_sleep_enable_ext0_wakeup(BUTTON_SLEEP, 0);
        
        Serial.println("Entering light sleep");
        Serial.flush();
        
        esp_light_sleep_start();
        
        // After waking up
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        
        // Restore PWM configuration
        ledcAttachPin(TFT_BL, PWM_CHANNEL);
        ledcWrite(PWM_CHANNEL, currentBrightness);
        
        if(wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
            Serial.println("Woken up by button");
            button.tick();
        } else {
            Serial.println("Woken up by timer");
        }
    } else {
        Serial.println("Not entering light sleep, brightness too low, flickering may occur");
        setCpuFrequencyMhz(80);
        Serial.println("CPU:" + String(getCpuFrequencyMhz()) + "MHz");
    }

}

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

    // Button Setup
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Set up button callbacks
    button.setClickMs(500); // 500ms for single click
    button.attachClick(cycleBrightness);
    button.attachDoubleClick(switchStation);
    button.attachMultiClick(startConfigPortal);
    button.setPressMs(10000); // 10 seconds for long press
    button.attachLongPressStart(handleLongPress);

    // Call WiFiManager setup
    setupWiFiManager();

    // Set custom brightness
    currentBrightnessIndex = config.defaultBrightness; //setting initial brightness from setup
    updateBrightness();
         
    // Initialize time client
    timeClient.begin();
    timeClient.setUpdateInterval(3600000); // Update every 60 minutes (3600000)
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
    // lightSleep();
    Serial.println("============ End of startup ==================");
}

// Global variables for timing
unsigned long lastUpdate = 0;


void loop() {
    button.tick();
    unsigned long currentMillis = millis();
    static unsigned long updateStartTime = 0;
    static bool isUpdating = false;
    
    if(portalRunning){
        wm.process();
    }

    handleOTA();

    if (!otaMode) {
        if (!isUpdating && currentMillis - lastUpdate >= UPDATE_INTERVAL) {
            setCpuFrequencyMhz(240);
            if (WiFi.status() == WL_CONNECTED) {
                drawCurrentTime();
                drawStationboard();
                drawBTC();
                debugInfo();
                Serial.println("============ End of refresh cycle ==================");
                updateStartTime = currentMillis;
                isUpdating = true;
            } else {
                displayStatus(false);
                Serial.println("WiFi disconnected, attempting reconnection...");
                WiFi.disconnect();
                WiFi.reconnect();
                delay(1000);
            }
        }
        
        // Check if update display time is over
        if (isUpdating && currentMillis - updateStartTime >= UPDATE_DURATION) {
            lightSleep();
            lastUpdate = currentMillis;
            isUpdating = false;
        }
    }
}
