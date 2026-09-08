#include "ota.h"
#include "globals.h"
#include "nightmode.h"
#include "utilities.h"
#include <WiFi.h>

int ota_progress_millis = 0;
WebServer server(80);
bool otaMode = false;

namespace {
OtaStateMachine otaState;
bool otaRoutesRegistered = false;
}

bool otaCredentialsConfigured() {
    return OTA_USERNAME[0] != '\0' && OTA_PASSWORD[0] != '\0';
}

void onOTAStart() {
    otaUploadStarted(otaState, millis());
    Serial.println("OTA update started!");
}
  
void onOTAProgress(size_t current, size_t final) {
    otaUploadProgressed(otaState, millis());
    if (millis() - ota_progress_millis > 1000) {
        ota_progress_millis = millis();
        Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
    }
}

void onOTAEnd(bool success) {
    otaUploadFinished(otaState, success);
    if (success) {
        Serial.println("OTA update finished successfully!");
    } else {
        Serial.println("There was an error during OTA update!");
    }
}

void stopOTA(const char* reason) {
    if (!otaMode) return;

    Serial.printf("Stopping OTA: %s\n", reason);
    server.stop();
    otaMode = false;
    otaState.current = OtaState::Inactive;
    setCpuFrequencyMhz(80);
    if (nightMode.active && !nightMode.temporaryWake) {
        ledcWrite(PWM_CHANNEL, 0);
    } else {
        updateBrightness();
    }
    forceRefresh = true;
}

void handleLongPress() {
    if (otaMode) {
        return;
    }
    if (!otaCanStart(portalRunning, otaCredentialsConfigured())) {
        Serial.println(portalRunning ? "OTA refused: config portal active"
                                     : "OTA disabled: OTA_USERNAME and OTA_PASSWORD are not configured");
        return;
    }
    
    setCpuFrequencyMhz(240);
    otaMode = true;
    otaActivate(otaState, millis());
    if (!otaRoutesRegistered) {
        server.on("/", HTTP_GET, []() {
            server.send(200, "text/html", "<a href='/update'>Update</a>");
        });
        ElegantOTA.begin(&server, OTA_USERNAME, OTA_PASSWORD);
        ElegantOTA.onStart(onOTAStart);
        ElegantOTA.onProgress(onOTAProgress);
        ElegantOTA.onEnd(onOTAEnd);
        otaRoutesRegistered = true;
    }
    server.begin();

    tft.loadFont(AA_FONT_SMALL);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Update Mode",20, 80);
    tft.drawString("To update, point your browser to:", 20, 120);
    tft.drawString("http://" + WiFi.localIP().toString() + "/update", 20, 140);
}

void handleOTA() {
    if (!otaMode) return;

    if (otaMustStop(otaState, millis(), WiFi.status() == WL_CONNECTED)) {
        stopOTA(otaState.current == OtaState::Failed ? "upload failed" : "timeout or WiFi loss");
        return;
    }
    server.handleClient();
    ElegantOTA.loop();
}
