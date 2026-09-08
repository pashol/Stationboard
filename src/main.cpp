#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <sys/time.h>
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
        esp_err_t timerResult = esp_sleep_enable_timer_wakeup(sleepDuration);
        esp_err_t ext0Result = esp_sleep_enable_ext0_wakeup(BUTTON_SLEEP, 0);
        if (timerResult != ESP_OK || ext0Result != ESP_OK) {
            Serial.printf("Light sleep setup failed: timer=%d ext0=%d\n", timerResult, ext0Result);
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
            ledcAttachPin(TFT_BL, PWM_CHANNEL);
            if (!nightMode.active || nightMode.temporaryWake) {
                updateBrightness();
            } else {
                ledcWrite(PWM_CHANNEL, 0);
            }
            setCpuFrequencyMhz(80);
            delay(100);
            return;
        }
        // Timer and button are the intended wake sources; network traffic must not wake us.

        Serial.println("Entering light sleep");
        Serial.flush();

        esp_err_t sleepResult = esp_light_sleep_start();
        if (sleepResult != ESP_OK) {
            Serial.printf("Light sleep start failed: %d\n", sleepResult);
            ledcAttachPin(TFT_BL, PWM_CHANNEL);
            if (!nightMode.active || nightMode.temporaryWake) {
                updateBrightness();
            } else {
                ledcWrite(PWM_CHANNEL, 0);
            }
            setCpuFrequencyMhz(80);
            delay(100);
            return;
        }
        
        // After waking up
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        WakeSource wakeSource = WakeSource::Other;
        if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
            wakeSource = WakeSource::Timer;
        } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
            wakeSource = WakeSource::Ext0;
        } else if (wakeup_reason == ESP_SLEEP_WAKEUP_WIFI) {
            wakeSource = WakeSource::WiFi;
        }
        const WakeAction wakeAction = wakeActionFor(wakeSource);
        
        if (wakeAction == WakeAction::Button && nightMode.active) {
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
        
        if (wakeAction == WakeAction::Button) {
            Serial.println("Woken up by button");
            button.tick();
        } else if (wakeAction == WakeAction::Timer) {
            Serial.println("Woken up by timer");
        } else if (wakeSource == WakeSource::WiFi) {
            Serial.println("Unexpected WiFi wake; WiFi wake is disabled");
        } else {
            Serial.printf("Woken up by unexpected cause: %d\n", wakeup_reason);
        }
    } else {
        Serial.println("No light sleep, reduce CPU frequency");
        setCpuFrequencyMhz(80);
        Serial.println("CPU:" + String(getCpuFrequencyMhz()) + "MHz");
    }

}

void serviceWiFiReconnect() {
    static ReconnectState reconnectState;
    const bool connected = WiFi.status() == WL_CONNECTED;

    if (observeWiFiRecovery(reconnectState, connected)) {
        forceRefresh = true;
    }
    if (shouldRequestWiFiReconnect(connected, reconnectState, millis())) {
        Serial.println("WiFi not connected, reconnecting...");
        WiFi.reconnect();
        recordReconnectFailure(reconnectState, millis());
    }
}

bool updateClock() {
    lastClockAttempt = millis();
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    const bool updated = timeClient.update();
    const time_t epoch = timeClient.getEpochTime();
    bool systemTimeSet = false;
    if (updated && isPlausibleEpoch(epoch)) {
        const timeval systemTime = timevalFromEpoch(epoch);
        systemTimeSet = settimeofday(&systemTime, nullptr) == 0;
    }
    clockValid = clockValidityAfterSync(clockValid, updated, epoch, systemTimeSet);
    if (systemTimeSet) return true;

    Serial.printf("Clock update failed: updated=%d epoch=%lld set=%d\n", updated,
                  static_cast<long long>(epoch), systemTimeSet);
    return false;
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
    button.setClickMs(BUTTON_CLICK_MS);
    button.attachClick(cycleBrightness);
    button.attachDoubleClick(switchStation);
    button.attachMultiClick(startConfigPortal);
    button.setPressMs(10000); // 10 seconds for long press
    button.attachLongPressStart(handleLongPress);

    // Call WiFiManager setup
    setupWiFiManager();
    WiFi.setAutoReconnect(true);
    esp_wifi_set_ps(WIFI_PS_NONE); // Disable WiFi power save mode
    
    // Set custom brightness
    currentBrightnessIndex = config.defaultBrightness; //setting initial brightness from setup
    updateBrightness();
         
    // Initialize time client (UTC - DST conversion handled by Timezone library)
    timeClient.begin();
    timeClient.setUpdateInterval(3600000); // Update every 60 minutes (3600000)
    updateClock();

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

    if (clockRetryDue(lastClockAttempt, currentMillis)) {
        updateClock();
    }

    // Expire cached rows independently of WiFi or the refresh scheduler.
    expireStationboardIfStale(currentMillis);
    expireConnectionsIfExpired(static_cast<int64_t>(timeClient.getEpochTime()));
    
    // Check night mode state (handles entering/exiting night mode based on time)
    checkNightMode();
    
    // Update night mode display (checks if temporary wake should end)
    updateNightModeDisplay();
    
    if (portalRunning) {
        wm.process();
    }

    // OTA remains serviceable across night-mode transitions while an upload is active.
    handleOTA();

    if (!otaMode) {
        serviceWiFiReconnect();

        // Determine update interval based on night mode
        unsigned long currentInterval = nightMode.active ? NIGHT_CHECK_INTERVAL : UPDATE_INTERVAL;

        const bool wasForced = forceRefresh;
        if (!isUpdating && (wasForced || shouldAttemptRefresh(lastUpdate, currentMillis, currentInterval))) {
            if (getCpuFrequencyMhz() != 240) setCpuFrequencyMhz(240); // Set CPU frequency to 240MHz

            if (WiFi.status() == WL_CONNECTED) {
                // Update time only when display is allowed to render
                if (!nightMode.active || nightMode.temporaryWake || forceRefresh) {
                    drawCurrentTime();
                }

                // Only update stationboard and BTC if not in night mode or during temporary wake
                // AND if config portal is not running
                if ((!nightMode.active || nightMode.temporaryWake || forceRefresh) && !portalRunning) {
                    RefreshResult refresh = refreshCurrentView();
                    debugInfo();
                    Serial.println("============ End of refresh cycle ==================");
                    forceRefresh = shouldRetryForcedRefresh(
                        wasForced, isTransportFreshResult(refresh.transport));
                }

                updateStartTime = currentMillis;
                isUpdating = true;
            } else {
                displayStatus(false);
                lastUpdate = currentMillis;
                updateStartTime = currentMillis;
                isUpdating = true;
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
