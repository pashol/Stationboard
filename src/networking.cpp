#include "networking.h"
#include <WiFi.h>
#include <TFT_eSPI.h>
#include "globals.h"

extern WiFiManager wm;
extern Config config;
extern TFT_eSPI tft;
extern const unsigned long HTTP_TIMEOUT;
extern const char* getBTCAPI;

// Define HTTP_CODE_OK if it's not already defined
#ifndef HTTP_CODE_OK
#define HTTP_CODE_OK 200
#endif

void setupWiFiManager() {
    // Check for reset trigger first
    Serial.println("Entering reset routine");
    checkForConfigReset();

    // Load existing configuration
    Serial.println("Trying to load config from file");
    loadConfiguration();

    // Set config save notify callback
    wm.setSaveConfigCallback(saveConfigCallback);

    // Add custom parameters for transport display settings
    WiFiManagerParameter custom_station_id("station", "Station ID 1", String(config.stationId).c_str(), 150);
    WiFiManagerParameter custom_station_id2("station2", "Station ID 2", String(config.stationId2).c_str(), 150);
    WiFiManagerParameter custom_limit("limit", "Number of Entries", String(config.limit).c_str(), 2);
    WiFiManagerParameter custom_offset("offset", "Time to station (min)", String(config.offset).c_str(), 2);
    WiFiManagerParameter custom_brightness("defaultBrightness", "Brightness level (0=off to 4=max)", String(config.defaultBrightness).c_str(), 1);
    
    wm.addParameter(&custom_station_id);
    wm.addParameter(&custom_station_id2);
    wm.addParameter(&custom_limit);
    wm.addParameter(&custom_offset);
    wm.addParameter(&custom_brightness);

    // Customize the configuration portal
    wm.setTitle("Transport Display Setup");
    
    // Enable debug output
    wm.setDebugOutput(true);
    
    // Set dark theme
    wm.setClass("invert");

    // Set configuration portal timeout (optional, in seconds)
    wm.setConfigPortalTimeout(600);

    // Start the configuration portal
    if (!wm.autoConnect("TransportDisplay_AP")) {
        tft.drawString("To config your Stationboard:", 10, 110);
        tft.drawString("Connect to access point TransportDisplay_AP", 10, 130);
        Serial.println("Failed to connect and hit timeout");

        delay(3000);
        ESP.restart();
        delay(5000);
    } else {
        // If you get here you have connected to the WiFi
        tft.drawString("Successfully connected to WiFi network!", 20, 100);
        Serial.println("Successfully connected to WiFi network!");
    }

    // Read updated parameters
    config.stationId = custom_station_id.getValue();
    config.stationId2 = custom_station_id2.getValue();
    config.limit = String(custom_limit.getValue()).toInt();
    config.offset = String(custom_offset.getValue()).toInt();
    config.defaultBrightness = String(custom_brightness.getValue()).toInt();

    // Save the custom parameters to config
    if (shouldSaveConfig) {
        saveConfiguration();
    }
}

void drawBTC() {
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT);
    http.setTimeout(HTTP_TIMEOUT);
    http.begin(getBTCAPI);
    
    int httpCode = http.GET();
    Serial.print("HTTPCODE: ");
    Serial.println(httpCode);

    String bitcoin_price = "N/A";
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error && doc.containsKey("data") && doc["data"].containsKey("amount")) {
            bitcoin_price = doc["data"]["amount"].as<String>().toInt();
        }
        displayStatus(true);
    } else {
        displayStatus(false);
    }
    
    http.end();

    // Create temporary sprite for BTC price display
    TFT_eSprite btcSprite(&tft);
    btcSprite.setColorDepth(8);
    btcSprite.createSprite((tft.width() / 2) - 25, 25);
    btcSprite.loadFont(AA_FONT_SMALL);
    
    // Clear sprite and set background
    btcSprite.fillSprite(TFT_WHITE);
    
    // Draw BTC price
    btcSprite.setTextDatum(TR_DATUM);
    btcSprite.setTextColor(TFT_BLACK, TFT_WHITE);
    btcSprite.drawString("BTC $" + bitcoin_price, btcSprite.width(), 5);
    
    // Push to bottom-right of screen
    btcSprite.pushSprite(tft.width() / 2, tft.height() - 25);
    
    Serial.print("Bitcoin Price: ");
    Serial.println(bitcoin_price);
}
