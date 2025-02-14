#include "ota.h"
#include "globals.h"
#include <WiFi.h>

int ota_progress_millis = 0;
WebServer server(80);
bool otaMode = false;

void onOTAStart() {
    Serial.println("OTA update started!");
}
  
void onOTAProgress(size_t current, size_t final) {
    if (millis() - ota_progress_millis > 1000) {
        ota_progress_millis = millis();
        Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
    }
}

void onOTAEnd(bool success) {
    if (success) {
        Serial.println("OTA update finished successfully!");
    } else {
        Serial.println("There was an error during OTA update!");
    }
}

void handleLongPress() {
    if (!otaMode) {
        setCpuFrequencyMhz(240);
        otaMode = true;
        server.on("/", HTTP_GET, []() {
            server.send(200, "text/html", "<a href='/update'>Update</a>");
        });
        ElegantOTA.begin(&server);
        // ElegantOTA callbacks
        ElegantOTA.onStart(onOTAStart);
        ElegantOTA.onProgress(onOTAProgress);
        ElegantOTA.onEnd(onOTAEnd);
        server.begin();
        
        tft.loadFont(AA_FONT_SMALL);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Update Mode",20, 80);
        tft.drawString("To update, point your browser to:", 20, 120);
        tft.drawString("http://" + WiFi.localIP().toString() + "/update", 20, 140);
    }
}

void handleOTA() {
    if (otaMode) {
        server.handleClient();
        ElegantOTA.loop();
    }
}
