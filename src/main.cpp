#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include "globals.h"
#include "networking.h"
#include "utilities.h"
#include "stationboard.h"
#include "ota.h"
#include "nightmode.h"

#define BUTTON_SLEEP GPIO_NUM_0  // Boot Button

void lightSleep() {
    Serial.println("Preparing for sleep...");
    
    // Determine sleep duration based on night mode
    unsigned long sleepDuration = inNightMode ? NIGHT_CHECK_INTERVAL * 1000ULL : SLEEP_DURATION;
    
    if (currentBrightnessIndex > 3 || inNightMode) {
        // Configure wake-up sources
        esp_sleep_enable_timer_wakeup(sleepDuration);
        esp_sleep_enable_ext0_wakeup(BUTTON_SLEEP, 0);
        esp_sleep_enable_wifi_wakeup();
        
        Serial.println("Entering light sleep");
        Serial.flush();
        
        esp_light_sleep_start();
        
        // After waking up
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        
        if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 && inNightMode) {
            temporaryNightWake = true;
            nightWakeStartTime = millis();
        }

        // Restore PWM configuration
        ledcAttachPin(TFT_BL, PWM_CHANNEL);
        if (!inNightMode || temporaryNightWake) {
            Serial.printf("Wake brightness restore: night=%d tempWake=%d\n", inNightMode, temporaryNightWake);
            updateBrightness();
        } else {
            Serial.printf("Wake keep dark: night=%d tempWake=%d\n", inNightMode, temporaryNightWake);
            ledcWrite(PWM_CHANNEL, 0);
        }
        
        if(wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
            Serial.println("Woken up by button");
            button.tick();
        } else {
            Serial.println("Woken up by timer");
        }
    } else {
        Serial.println("No light sleep, reduce CPU frequency");
        setCpuFrequencyMhz(80);
        Serial.println("CPU:" + String(getCpuFrequencyMhz()) + "MHz");
    }

}

void reconnectWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, reconnecting...");
        WiFi.disconnect();
        delay(100);
        WiFi.reconnect();
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(500);
            attempts++;
        }
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
    esp_wifi_set_ps(WIFI_PS_NONE); // Disable WiFi power save mode
    
    // Set custom brightness
    currentBrightnessIndex = config.defaultBrightness; //setting initial brightness from setup
    updateBrightness();
         
    // Initialize time client (UTC - DST conversion handled by Timezone library)
    timeClient.begin();
    timeClient.setUpdateInterval(3600000); // Update every 60 minutes (3600000)

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
    Serial.println("============ End of setup ==================");
}

// Global variables for timing
unsigned long lastUpdate = 0;


void loop() {
    button.tick();
    unsigned long currentMillis = millis();
    static unsigned long updateStartTime = 0;
    static bool isUpdating = false;
    
    // Check night mode state (handles entering/exiting night mode based on time)
    checkNightMode();
    
    // Update night mode display (checks if temporary wake should end)
    updateNightModeDisplay();
    
    if(portalRunning){
        wm.process();
    }

    // OTA is disabled during night mode
    if (!inNightMode) {
        handleOTA();
    }

    if (!otaMode) {
        // Determine update interval based on night mode
        unsigned long currentInterval = inNightMode ? NIGHT_CHECK_INTERVAL : UPDATE_INTERVAL;
        
        if (forceRefresh || (!isUpdating && currentMillis - lastUpdate >= currentInterval)) {
            if (getCpuFrequencyMhz() != 240) setCpuFrequencyMhz(240); // Set CPU frequency to 240MHz

            reconnectWiFi();

            if (WiFi.status() == WL_CONNECTED) {
                // Update time only when display is allowed to render
                if (!inNightMode || temporaryNightWake || forceRefresh) {
                    drawCurrentTime();
                }

                // Only update stationboard and BTC if not in night mode or during temporary wake
                // AND if config portal is not running
                if ((!inNightMode || temporaryNightWake || forceRefresh) && !portalRunning) {
                    drawStationboard();
                    drawBTC();
                    debugInfo();
                    Serial.println("============ End of refresh cycle ==================");
                }

                updateStartTime = currentMillis;
                isUpdating = true;
                forceRefresh = false;
            } else {
                displayStatus(false);
                forceRefresh = false;
            }
        }
        
        // Check if update display time is over
        if (isUpdating && currentMillis - updateStartTime >= UPDATE_DURATION) {
            if (!portalRunning && !(inNightMode && temporaryNightWake)) {
                lightSleep();
            }
            lastUpdate = currentMillis;
            isUpdating = false;
        }
    }
}
