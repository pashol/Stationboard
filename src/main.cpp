#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include "globals.h"
#include "networking.h"
#include "utilities.h"
#include "stationboard.h"
#include "connections.h"
#include "ota.h"
#include "nightmode.h"

#define BUTTON_SLEEP GPIO_NUM_0  // Boot Button

namespace {
RefreshResult refreshCurrentView() {
    FetchResult transport = displayMode == 2 ? fetchAndDrawConnections() : drawStationboard();
    FetchResult btc = drawBTC();
    RefreshResult refresh{transport, btc, millis()};

    // BTC is optional: its result must not override the transport verdict.
    displayStatus(isTransportFreshResult(refresh.transport));
    return refresh;
}
}

void lightSleep() {
    Serial.println("Preparing for sleep...");
    
    // Determine sleep duration based on night mode
    unsigned long sleepDuration = nightMode.active ? NIGHT_CHECK_INTERVAL * 1000ULL : SLEEP_DURATION;

    if (currentBrightnessIndex > 3 || nightMode.active) {
        // Configure wake-up sources
        esp_sleep_enable_timer_wakeup(sleepDuration);
        esp_sleep_enable_ext0_wakeup(BUTTON_SLEEP, 0);
        esp_sleep_enable_wifi_wakeup();
        
        Serial.println("Entering light sleep");
        Serial.flush();
        
        esp_light_sleep_start();
        
        // After waking up
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        
        if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 && nightMode.active) {
            nightMode.temporaryWake = true;
            nightMode.wakeStartTime = millis();
        }

        // Restore PWM configuration
        ledcAttachPin(TFT_BL, PWM_CHANNEL);
        if (!nightMode.active || nightMode.temporaryWake) {
            Serial.printf("Wake brightness restore: night=%d tempWake=%d\n", nightMode.active, nightMode.temporaryWake);
            updateBrightness();
        } else {
            Serial.printf("Wake keep dark: night=%d tempWake=%d\n", nightMode.active, nightMode.temporaryWake);
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

void serviceWiFiReconnect(unsigned long now) {
    static ReconnectState reconnectState;
    static bool connectionStateKnown = false;
    static bool wasConnected = false;
    const bool connected = WiFi.status() == WL_CONNECTED;

    if (connected) {
        recordReconnectSuccess(reconnectState);
        if (connectionStateKnown && !wasConnected) {
            forceRefresh = true;
        }
    } else if (reconnectDue(reconnectState, now)) {
        Serial.println("WiFi not connected, reconnecting...");
        WiFi.reconnect();
        recordReconnectFailure(reconnectState, now);
    }

    wasConnected = connected;
    connectionStateKnown = true;
}

void setup() {
    Serial.begin(115200);

    // Mount without formatting: a failed mount must never wipe the
    // filesystem at boot. Formatting happens only in the explicit
    // boot-button recovery flow (checkForConfigReset).
    bool fsMounted = SPIFFS.begin(false);
    if (fsMounted) {
        Serial.println("SPIFFS Mounted");
    } else {
        Serial.println("Filesystem mount failed - continuing with defaults (no format)");
    }

    // Initialize display
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);

    if (!fsMounted) {
        tft.loadFont(AA_FONT_SMALL);
        tft.drawString("Filesystem error", 20, 120);
        tft.drawString("Using defaults", 20, 140);
    }

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

    // Keep the offline status rendered by setupWiFiManager until WiFi recovers.
    if (WiFi.status() == WL_CONNECTED) {
        tft.fillScreen(TFT_BLUE);
        tft.fillRect(0, tft.height() - 25 , tft.width(), 25, TFT_WHITE); //footer
        tft.loadFont(AA_FONT_SMALL);
        tft.setTextColor(TFT_WHITE, TFT_BLUE);
        drawCurrentTime();
        refreshCurrentView();
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

    // Expire cached rows independently of WiFi or the refresh scheduler.
    expireStationboardIfStale(currentMillis);
    expireConnectionsIfExpired(static_cast<int64_t>(timeClient.getEpochTime()));
    
    // Check night mode state (handles entering/exiting night mode based on time)
    checkNightMode();
    
    // Update night mode display (checks if temporary wake should end)
    updateNightModeDisplay();
    
    if(portalRunning){
        wm.process();
    }

    // OTA is disabled during night mode
    if (!nightMode.active) {
        handleOTA();
    }

    if (!otaMode) {
        serviceWiFiReconnect(currentMillis);

        // Determine update interval based on night mode
        unsigned long currentInterval = nightMode.active ? NIGHT_CHECK_INTERVAL : UPDATE_INTERVAL;

        if (forceRefresh || (!isUpdating && shouldAttemptRefresh(lastUpdate, currentMillis, currentInterval))) {
            if (getCpuFrequencyMhz() != 240) setCpuFrequencyMhz(240); // Set CPU frequency to 240MHz

            if (WiFi.status() == WL_CONNECTED) {
                // Update time only when display is allowed to render
                if (!nightMode.active || nightMode.temporaryWake || forceRefresh) {
                    drawCurrentTime();
                }

                // Only update stationboard and BTC if not in night mode or during temporary wake
                // AND if config portal is not running
                if ((!nightMode.active || nightMode.temporaryWake || forceRefresh) && !portalRunning) {
                    refreshCurrentView();
                    debugInfo();
                    Serial.println("============ End of refresh cycle ==================");
                }

                updateStartTime = currentMillis;
                isUpdating = true;
                forceRefresh = false;
            } else {
                displayStatus(false);
                lastUpdate = currentMillis;
                forceRefresh = false;
            }
        }
        
        // Check if update display time is over
        if (isUpdating && currentMillis - updateStartTime >= UPDATE_DURATION) {
            if (!portalRunning && !(nightMode.active && nightMode.temporaryWake)) {
                lightSleep();
            }
            lastUpdate = currentMillis;
            isUpdating = false;
        }
    }
}
