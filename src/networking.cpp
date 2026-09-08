#include "networking.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include "globals.h"
#include "tls_certs.h"

extern WiFiManager wm;
extern Config config;
extern TFT_eSPI tft;
extern int displayMode;
extern bool forceRefresh;
extern const unsigned long HTTP_TIMEOUT;
extern const char* getBTCAPI;

namespace {
bool httpStreamEof(Stream& stream) {
    return !static_cast<WiFiClientSecure&>(stream).connected();
}
}

void onConfigPortalStart(WiFiManager* myWiFiManager) {
    tft.fillScreen(TFT_BLACK);
    tft.loadFont(AA_FONT_SMALL);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Stationboard v" FIRMWARE_VERSION, 20, 20);
    tft.drawString("WiFi Setup Required", 20, 40);
    tft.drawString("1. Connect phone to WiFi:", 20, 70);
    tft.drawString("   Stationboard_AP", 20, 90);
    tft.drawString("2. Open browser:", 20, 115);
    tft.drawString("   192.168.4.1", 20, 135);
}

// Task 4: portal parameters with program lifetime.
//
// WiFiManager keeps raw pointers to every registered parameter (and to each
// custom-HTML string), including across startWebPortal()/process() calls.
// The previous stack-local parameters dangled as soon as
// setupWiFiManager() returned. All 15 parameters below therefore live with
// static storage duration and are registered with wm.addParameter() exactly
// once; their displayed values are refreshed from the live config via
// setValue() (which copies into the parameter's own buffer) before each
// portal open.
namespace {

const char portalWelcomeHTML[] = ""
    "<div style='text-align:left; padding:15px; margin:10px; background:#666; color:white; border-radius:4px'>"
    "<h2>Welcome to Stationboard Setup!</h2>"
    "<p><small>Firmware v" FIRMWARE_VERSION "</small></p>"
    "<p>This device shows real-time public transport departures for Swiss stations.</p>"
    "<p><b>To configure your display:</b></p>"
    "<ol>"
    "<li>Enter your WiFi credentials</li>"
    "<li>Set your station ID (doesn't need to be exact)</li>"
    "<li>Configure display preferences</li>"
    "<li>For firmware updates, press the button for 10 seconds on the main screen</li>"
    "</ol>"
    "<p><b>Need help? Contact:</b></p>"
    "<p>✉️ pascal.holzmann@gmail.com</p>"
    "<p>🌐 https://github.com/pashol/Stationboard</p>"
    "</div>";

const char portalNightModeHTML[] = ""
    "<br/><hr/><br/>"
    "<h3>Night Mode Settings</h3>"
    "<p>Automatically turn off display during night hours to save power and reduce light pollution.</p>";

const char portalConnectionsHTML[] = ""
    "<br/><hr/><br/>"
    "<h3>Connections Mode</h3>"
    "<p>Enable a third display mode showing journeys from Station 1 to Station 2.</p>";

WiFiManagerParameter welcomeParam(portalWelcomeHTML);
WiFiManagerParameter stationParam("station", "Station ID 1", "", 150);
WiFiManagerParameter station2Param("station2", "Station ID 2", "", 150);
WiFiManagerParameter limitParam("limit", "Number of Entries", "", 2);
WiFiManagerParameter offsetParam("offset", "Time to station (min)", "", 4);
WiFiManagerParameter brightnessParam("defaultBrightness", "Brightness level (0=off to 4=max)", "", 1);
WiFiManagerParameter nightModeHtmlParam(portalNightModeHTML);
WiFiManagerParameter nightEnabledParam("nightModeEnabled", "Enable Night Mode (0 or 1)", "", 1);
WiFiManagerParameter nightStartHourParam("nightModeStartHour", "Start Hour (0-23)", "", 2);
WiFiManagerParameter nightStartMinuteParam("nightModeStartMinute", "Start Minute (0-59)", "", 2);
WiFiManagerParameter nightEndHourParam("nightModeEndHour", "End Hour (0-23)", "", 2);
WiFiManagerParameter nightEndMinuteParam("nightModeEndMinute", "End Minute (0-59)", "", 2);
WiFiManagerParameter nightWeekendParam("nightModeWeekendDisable", "Disable on weekends (0 or 1)", "", 1);
WiFiManagerParameter connectionsHtmlParam(portalConnectionsHTML);
WiFiManagerParameter connectionsParam("connectionsEnabled", "Enable Connections Mode (0 or 1)", "", 1);

bool portalParametersRegistered = false;

const char* portalParamValue(const WiFiManagerParameter& p) {
    const char* v = p.getValue();
    return v != nullptr ? v : "";
}

}  // namespace

void refreshPortalParameters() {
    stationParam.setValue(config.stationId.c_str(), 150);
    station2Param.setValue(config.stationId2.c_str(), 150);
    char num[8];
    snprintf(num, sizeof(num), "%d", config.limit);
    limitParam.setValue(num, 2);
    snprintf(num, sizeof(num), "%d", config.offset);
    offsetParam.setValue(num, 4);
    snprintf(num, sizeof(num), "%d", config.defaultBrightness);
    brightnessParam.setValue(num, 1);
    nightEnabledParam.setValue(config.nightModeEnabled ? "1" : "0", 1);
    snprintf(num, sizeof(num), "%d", config.nightModeStartHour);
    nightStartHourParam.setValue(num, 2);
    snprintf(num, sizeof(num), "%d", config.nightModeStartMinute);
    nightStartMinuteParam.setValue(num, 2);
    snprintf(num, sizeof(num), "%d", config.nightModeEndHour);
    nightEndHourParam.setValue(num, 2);
    snprintf(num, sizeof(num), "%d", config.nightModeEndMinute);
    nightEndMinuteParam.setValue(num, 2);
    nightWeekendParam.setValue(config.nightModeWeekendDisable ? "1" : "0", 1);
    connectionsParam.setValue(config.connectionsEnabled ? "1" : "0", 1);
}

// Shared portal-ingress path (Task 4, Step 4): parse the persistent
// parameters into a temporary Config, normalize + validate (Task 2), then —
// only on success — assign the global config first (saveConfiguration reads
// the global, so assignment precedes persist), persist when requested and
// revert to the previous config on save failure, then normalize displayMode
// into range and request a refresh. Any failure retains the prior config
// and logs. Used identically by the save-params callback (runtime portal
// saves) and the boot-time read path in setupWiFiManager(), so flash loads
// and portal submissions are validated identically.
static bool applyPortalParametersToConfig(bool persist) {
    Config candidate;
    candidate.stationId = portalParamValue(stationParam);
    candidate.stationId2 = portalParamValue(station2Param);
    candidate.limit = String(portalParamValue(limitParam)).toInt();
    candidate.offset = String(portalParamValue(offsetParam)).toInt();
    candidate.defaultBrightness = String(portalParamValue(brightnessParam)).toInt();
    candidate.nightModeEnabled = String(portalParamValue(nightEnabledParam)).toInt() != 0;
    candidate.nightModeStartHour = String(portalParamValue(nightStartHourParam)).toInt();
    candidate.nightModeStartMinute = String(portalParamValue(nightStartMinuteParam)).toInt();
    candidate.nightModeEndHour = String(portalParamValue(nightEndHourParam)).toInt();
    candidate.nightModeEndMinute = String(portalParamValue(nightEndMinuteParam)).toInt();
    candidate.nightModeWeekendDisable = String(portalParamValue(nightWeekendParam)).toInt() != 0;
    candidate.connectionsEnabled = String(portalParamValue(connectionsParam)).toInt() != 0;

    normalizeConfiguration(candidate);
    if (!validateConfiguration(candidate)) {
        Serial.println("Portal parameters invalid - keeping previous config");
        return false;
    }
    Config previous = config;
    config = candidate;
    if (persist && !saveConfiguration()) {
        Serial.println("Portal config save failed - keeping previous config");
        config = previous;
        return false;
    }
    displayMode = constrain(displayMode, 0, config.connectionsEnabled ? 2 : 1);
    forceRefresh = true;
    return true;
}

void registerPortalParametersOnce() {
    if (portalParametersRegistered) {
        return;
    }
    wm.addParameter(&welcomeParam);
    wm.addParameter(&stationParam);
    wm.addParameter(&station2Param);
    wm.addParameter(&limitParam);
    wm.addParameter(&offsetParam);
    wm.addParameter(&brightnessParam);
    wm.addParameter(&nightModeHtmlParam);
    wm.addParameter(&nightEnabledParam);
    wm.addParameter(&nightStartHourParam);
    wm.addParameter(&nightStartMinuteParam);
    wm.addParameter(&nightEndHourParam);
    wm.addParameter(&nightEndMinuteParam);
    wm.addParameter(&nightWeekendParam);
    wm.addParameter(&connectionsHtmlParam);
    wm.addParameter(&connectionsParam);
    // Runtime portal saves validate + persist + apply without rebooting.
    // This must not disturb the boot autoConnect flow: it only fires when
    // the user submits the portal form.
    wm.setSaveParamsCallback([]() { applyPortalParametersToConfig(true); });
    portalParametersRegistered = true;
}

// Task 4: runtime verifier proving the 15 portal parameters keep program
// lifetime. First call snapshots their addresses; every later call returns
// false if any address drifted (structurally impossible for the
// anonymous-namespace statics above, but OBSERVABLE here for Serial/Task 12
// soak checks instead of trusting the structure blindly).
bool portalParametersAreStable() {
    static bool baselineTaken = false;
    static const WiFiManagerParameter* baseline[15] = {nullptr};
    const WiFiManagerParameter* current[15] = {
        &welcomeParam, &stationParam, &station2Param, &limitParam,
        &offsetParam, &brightnessParam, &nightModeHtmlParam,
        &nightEnabledParam, &nightStartHourParam, &nightStartMinuteParam,
        &nightEndHourParam, &nightEndMinuteParam, &nightWeekendParam,
        &connectionsHtmlParam, &connectionsParam};
    if (!baselineTaken) {
        for (int i = 0; i < 15; i++) baseline[i] = current[i];
        baselineTaken = true;
        return true;
    }
    for (int i = 0; i < 15; i++) {
        if (baseline[i] != current[i]) return false;
    }
    return true;
}

void setupWiFiManager() {
    // Check for reset trigger first
    Serial.println("Entering reset routine");
    checkForConfigReset();

    // Load existing configuration
    Serial.println("Trying to load config from file");
    if (!loadConfiguration()) { Serial.println("Config load failed, using defaults"); }

    // Set config save notify callback
    wm.setSaveConfigCallback(saveConfigCallback);

    // Register program-lifetime parameters exactly once, then refresh their
    // displayed values from the live config before the portal can open.
    // Snapshot the program-lifetime addresses and immediately re-verify
    // (second call returns true); later Task 12 soak calls keep observing.
    registerPortalParametersOnce();
    portalParametersAreStable();
    if (!portalParametersAreStable()) {
        Serial.println("Portal parameters unstable - unexpected address drift");
    }
    refreshPortalParameters();

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

    // Register callback to show instructions when AP portal starts
    wm.setAPCallback(onConfigPortalStart);

    // Start the configuration portal
    if (!wm.autoConnect("Stationboard_AP")) {
        Serial.println("Failed to connect and hit timeout");

        delay(3000);
        ESP.restart();
        delay(5000);
    } else {
        // If you get here you have connected to the WiFi
        tft.drawString("Successfully connected to WiFi network!", 20, 100);
        Serial.println("Successfully connected to WiFi network!");
    }

    // Apply validated portal values through the same temp -> normalize ->
    // validate -> assign path the save-params callback uses; persist only
    // when the portal requested a save (shouldSaveConfig). On failure the
    // previously loaded config is retained.
    if (!applyPortalParametersToConfig(shouldSaveConfig)) {
        Serial.println("Portal parameters rejected - keeping loaded config");
    }
    shouldSaveConfig = false;
}

FetchResult drawBTC() {
    // Cert-validated HTTPS (Task 7): GTS Root R4 anchors api.coinbase.com
    // via the WE1 <- R4-cross <- GlobalSign chain (see tls_certs.h audit).
    // The previous http.begin(url) overload built an INSECURE client
    // internally; that silent downgrade is gone. No setInsecure.
    WiFiClientSecure client;
    client.setCACert(TLS_BTC_ROOT_CA);
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT);
    http.setTimeout(HTTP_TIMEOUT);
    http.useHTTP10(true);
    if (!http.begin(client, getBTCAPI)) {
        Serial.println("BTC HTTP setup failed");
        http.end();
        return FetchResult::HttpError;
    }

    const unsigned long requestStarted = millis();
    int httpCode = http.GET();
    Serial.print("HTTPCODE: ");
    Serial.println(httpCode);

    String bitcoin_price = "N/A";
    FetchResult result = FetchResult::HttpError;

    if (isExpired(requestStarted, millis(), HTTP_TOTAL_TIMEOUT)) {
        result = FetchResult::TimedOut;
    } else if (httpCode == HTTP_OK_STATUS) {
        int contentLength = http.getSize();
        if (!isContentLengthAllowed(contentLength, defaultLimits())) {
            result = FetchResult::TooLarge;
        } else {
            BoundedStream bounded(http.getStream(), defaultLimits(), requestStarted, httpStreamEof);
            String payload;
            size_t reserveBytes = contentLength >= 0 ? (size_t)contentLength : 1024;
            if (!payload.reserve(reserveBytes)) {
                result = FetchResult::OutOfMemory;
            } else {
                uint8_t buffer[128];
                size_t count;
                bool outOfMemory = false;
                while ((count = bounded.readBytes(buffer, sizeof(buffer))) > 0) {
                    if (!payload.concat((const char*)buffer, count)) {
                        outOfMemory = true;
                        break;
                    }
                }
                if (outOfMemory) {
                    result = FetchResult::OutOfMemory;
                } else if (bounded.limitReached()) {
                    Serial.printf("BTC response reached byte limit: %d bytes\n", (int)payload.length());
                    result = FetchResult::TooLarge;
                } else if (bounded.expired()) {
                    Serial.println("BTC response timed out");
                    result = FetchResult::TimedOut;
                } else if (!bounded.eofReached()) {
                    result = FetchResult::ParseError;
                } else {
                    String price;
                    bool priceOk = parseBtcPrice(payload, price);
                    result = btcVerdict(httpCode, priceOk);
                    if (priceOk) {
                        bitcoin_price = price;
                    }
                }
            }
        }
    } else {
        result = FetchResult::HttpError;
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
    // The caller aggregates this optional result with transport status.
    return result;
}
