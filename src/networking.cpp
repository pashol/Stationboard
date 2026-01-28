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

    // First, create the welcome message as a parameter
    const char* welcomeHTML = ""
        "<div style='text-align:left; padding:15px; margin:10px; background:#666; color:white; border-radius:4px'>"
        "<h2>Welcome to Stationboard Setup!</h2>"
        "<p>This device shows real-time public transport departures for Swiss stations.</p>"
        "<p><b>To configure your display:</b></p>"
        "<ol>"
        "<li>Enter your WiFi credentials</li>"
        "<li>Set your station ID (doesn't need to be exact)</li>"
        "<li>Configure display preferences</li>"
        "<li>For firmware updates press button for 10s on main screen</li>"
        "</ol>"
        "<p><b>Need help? Contact:</b></p>"
        "<p>✉️ pascal.holzmann@gmail.com</p>"
        "<p>🌐 https://github.com/pashol/Stationboard</p>"
        "</div>";
    
    // Add welcome message as a parameter FIRST
    WiFiManagerParameter custom_html(welcomeHTML);
    wm.addParameter(&custom_html);

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

    // Night mode section header
    const char* nightModeHTML = ""
        "<br/><hr/><br/>"
        "<h3>Night Mode Settings</h3>"
        "<p>Automatically turn off display during night hours to save power and reduce light pollution.</p>";
    WiFiManagerParameter custom_nightmode_html(nightModeHTML);
    wm.addParameter(&custom_nightmode_html);

    // Night mode parameters
    WiFiManagerParameter custom_nightmode_enabled("nightModeEnabled", "Enable Night Mode (0 or 1)", String(config.nightModeEnabled ? "1" : "0").c_str(), 1);
    WiFiManagerParameter custom_nightmode_start_hour("nightModeStartHour", "Start Hour (0-23)", String(config.nightModeStartHour).c_str(), 2);
    WiFiManagerParameter custom_nightmode_start_min("nightModeStartMinute", "Start Minute (0-59)", String(config.nightModeStartMinute).c_str(), 2);
    WiFiManagerParameter custom_nightmode_end_hour("nightModeEndHour", "End Hour (0-23)", String(config.nightModeEndHour).c_str(), 2);
    WiFiManagerParameter custom_nightmode_end_min("nightModeEndMinute", "End Minute (0-59)", String(config.nightModeEndMinute).c_str(), 2);
    WiFiManagerParameter custom_nightmode_weekend("nightModeWeekendDisable", "Disable on weekends (0 or 1)", String(config.nightModeWeekendDisable ? "1" : "0").c_str(), 1);
    
    wm.addParameter(&custom_nightmode_enabled);
    wm.addParameter(&custom_nightmode_start_hour);
    wm.addParameter(&custom_nightmode_start_min);
    wm.addParameter(&custom_nightmode_end_hour);
    wm.addParameter(&custom_nightmode_end_min);
    wm.addParameter(&custom_nightmode_weekend);

    // Customize the configuration portal
    wm.setTitle("Stationboard Setup");

    // Set dark theme
    wm.setClass("invert");

    // Disable firmware updates
    wm.setShowInfoUpdate(false);  // This disables the firmware update menu item
    
    // Enable debug output
    wm.setDebugOutput(true);
    
    // Set configuration portal timeout (optional, in seconds)
    wm.setConfigPortalTimeout(600);

    // Start the configuration portal
    if (!wm.autoConnect("Stationboard_AP")) {
        tft.drawString("To config your Stationboard:", 10, 110);
        tft.drawString("Connect mobile to 'Stationboard_AP'", 10, 130);
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
    
    // Night mode parameters
    config.nightModeEnabled = String(custom_nightmode_enabled.getValue()).toInt() != 0;
    config.nightModeStartHour = String(custom_nightmode_start_hour.getValue()).toInt();
    config.nightModeStartMinute = String(custom_nightmode_start_min.getValue()).toInt();
    config.nightModeEndHour = String(custom_nightmode_end_hour.getValue()).toInt();
    config.nightModeEndMinute = String(custom_nightmode_end_min.getValue()).toInt();
    config.nightModeWeekendDisable = String(custom_nightmode_weekend.getValue()).toInt() != 0;

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
