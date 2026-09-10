#include "stationboard.h"
#include "globals.h"
#include "utilities.h"
#include "http_request.h"
#include "tls_certs.h"
// #include "NotoSansBold15.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {
bool httpStreamEof(Stream& stream) {
    return !static_cast<WiFiClientSecure&>(stream).connected();
}

StationboardSnapshot currentSnapshots[2];
bool hasCurrentSnapshot[2] = {false, false};

}

void expireStationboardIfStale(unsigned long now) {
    if (displayMode == 2 || !clockValid) return;

    const int view = displayMode == 0 ? 0 : 1;
    if (!hasCurrentSnapshot[view] ||
        !pruneStationboardSnapshot(currentSnapshots[view], static_cast<int64_t>(timeClient.getEpochTime()))) {
        return;
    }

    drawStation(currentSnapshots[view].station);
    displayTransports(currentSnapshots[view]);
}

void drawTransport(TFT_eSprite& sprite, const Transport& transport, int yPos) {
    const char* LONG_DISTANCE[] = {"IR", "IC", "EC", "ICE", "ICN", "TGV"};
    const char* REGIONAL[] = {"S", "RE", "RB", "R", "T", "N", "SN"};
    const char* NIGHT[] = {"N", "SN"};
    if (transport.name == "null") return;

    // Format table row - content widths must match borders
    String line = transport.category + transport.number;
    line.trim();
    if (line.length() > 6) line = line.substring(0, 6);
    while (line.length() < 6) line += " ";
    
    String dest = transport.destination;
    if (dest.length() > 25) dest = dest.substring(0, 22) + "...";
    while (dest.length() < 25) dest += " ";
    
    String timeStr = transport.departure;
    while (timeStr.length() < 5) timeStr += " ";
    
    String delayStr = transport.delay.toInt() > 0 ? "+" + transport.delay : " ";
    while (delayStr.length() < 4) delayStr += " ";
    
    Serial.println("| " + line + " | " + dest + " | " + timeStr + " |" + delayStr + " |");

    sprite.setTextColor(TFT_WHITE, TFT_BLUE);
    sprite.drawString(transport.departure, POS_TIME, yPos + 1);
    
    if (transport.delay.toInt() > 0) {
        sprite.setTextColor(TFT_YELLOW, TFT_BLUE);
        sprite.drawString("+" + transport.delay, POS_DELAY, yPos + 1);
    }
    
    bool isLongDistance = std::any_of(std::begin(LONG_DISTANCE), std::end(LONG_DISTANCE),
        [&](const char* cat) { return transport.category == cat; });
    bool isRegional = std::any_of(std::begin(REGIONAL), std::end(REGIONAL),
        [&](const char* cat) { return transport.category == cat; });
    bool isNight = std::any_of(std::begin(NIGHT), std::end(NIGHT),
        [&](const char* cat) { return transport.category == cat; });

    if (isLongDistance) {
        sprite.setTextColor(TFT_WHITE, TFT_RED);
        sprite.fillRect(0, yPos, POS_TIME - POS_BUS - 1, POS_INC - 3, TFT_RED);
    } else if (isRegional) {
        sprite.setTextColor(TFT_BLUE, TFT_WHITE);
        sprite.fillRect(0, yPos, POS_TIME - POS_BUS - 1, POS_INC - 3, TFT_WHITE);
    } else if (isNight) {
        sprite.setTextColor(TFT_WHITE, TFT_BLACK);
        sprite.fillRect(0, yPos, POS_TIME - POS_BUS - 1, POS_INC - 3, TFT_BLACK);
    } else {
        sprite.setTextColor(TFT_WHITE, TFT_BLUE);
    }

    sprite.drawString(transport.category + transport.number, POS_BUS, yPos + 1);
    sprite.setTextColor(TFT_WHITE, TFT_BLUE);
    sprite.drawString(transport.destination, POS_TO, yPos + 1);
}

void displayTransports(const StationboardSnapshot& snapshot) {
    // Null-named rows are skipped per-row inside drawTransport; no copied
    // vector is built here. Loops are bounded to snapshot.count (which never
    // exceeds MAX_TRANSPORTS by the parser contract).
    size_t count = stationboardVisibleCount(snapshot, config.limit);

    // Print table header
    Serial.println("+--------+---------------------------+-------+------+");
    Serial.println("| Line   | Destination               | Time  |Delay |");
    Serial.println("+--------+---------------------------+-------+------+");

    TFT_eSprite sprite(&tft);
    sprite.setColorDepth(8);
    if (sprite.createSprite(tft.width(), 5 * POS_INC) == nullptr) {
        Serial.println("displayTransports: sprite allocation failed - keeping previous frame");
        return;
    }
    sprite.loadFont(AA_FONT_SMALL);

    // Draw first half (0-4)
    sprite.fillSprite(TFT_BLUE);
    for (size_t i = 0; i < count && i < 5; i++) {
        drawTransport(sprite, snapshot.rows[i], i * POS_INC);
    }
    sprite.pushSprite(0, POS_FIRST);

    // Draw second half (5-9)
    sprite.fillSprite(TFT_BLUE);
    for (size_t i = 5; i < count; i++) {
        drawTransport(sprite, snapshot.rows[i], (i-5) * POS_INC);
    }
    sprite.pushSprite(0, POS_FIRST + (5 * POS_INC));

    // Print table footer
    Serial.println("+--------+---------------------------+-------+------+");
    Serial.println();
}

void drawStation(const String& station) {
    tft.loadFont(AA_FONT_SMALL);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(0, 0, tft.width(), 25, TFT_WHITE);
    tft.drawString(station, POS_BUS, 7);
}

void renderStationboardCache() {
    const int view = displayMode == 0 ? 0 : 1;
    if (!hasCurrentSnapshot[view]) {
        drawStation(view == 0 ? config.stationId : config.stationId2);
        tft.fillRect(0, POS_FIRST, tft.width(), 10 * POS_INC, TFT_BLUE);
        return;
    }
    drawStation(currentSnapshots[view].station);
    displayTransports(currentSnapshots[view]);
}

FetchResult drawStationboard(const RequestLimits& limits) {
    WiFiClientSecure client;
    client.setCACert(TLS_TRANSPORT_ROOT_CA);
    HTTPClient http;
    http.setConnectTimeout(limits.inactivityMs);
    http.setTimeout(limits.inactivityMs);
    http.useHTTP10(true);

    String currentStationId = (displayMode == 0) ? config.stationId : config.stationId2;
    String relativeTime = getFormattedTimeRelativeToNow(config.offset);
    String url = buildStationboardUrl(currentStationId, stationboardRequestLimit(config.limit), relativeTime);

    Serial.println("Relative Time: " + relativeTime);
    Serial.print("URL: ");
    Serial.println(url);
    FetchResult result = FetchResult::HttpError;
    if (!http.begin(client, url)) {
        Serial.println("Stationboard HTTP setup failed");
        http.end();
        return result;
    }
    const unsigned long requestStarted = millis();
    int httpCode = http.GET();
    if (isExpired(requestStarted, millis(), limits.totalMs)) {
        result = FetchResult::TimedOut;
        Serial.printf("Stationboard fetch failed: %d\n", (int)result);
        http.end();
        return result;
    }
    if (httpCode != HTTP_OK_STATUS) {
        Serial.printf("Stationboard HTTP status: %d\n", httpCode);
        Serial.printf("Stationboard fetch failed: %d\n", (int)result);
        http.end();
        return result;
    }
    // Reject a known oversized body before parsing; unknown/misreported
    // lengths stay bounded by the BoundedStream byte cap below plus the
    // fixed 8KB document + filter.
    int contentLength = http.getSize();
    if (!isContentLengthAllowed(contentLength, limits)) {
        Serial.printf("Stationboard response too large: %d bytes\n", contentLength);
        result = FetchResult::TooLarge;
        Serial.printf("Stationboard fetch failed: %d\n", (int)result);
        http.end();
        return result;
    }
    StationboardSnapshot snapshot;
    BoundedStream bounded(http.getStream(), limits, requestStarted, httpStreamEof);
    bool ok = parseStationboard(bounded, snapshot);
    if (ok) ok = consumeToEnd(bounded);
    if (ok) {
        result = FetchResult::Success;
    } else if (bounded.limitReached()) {
        result = FetchResult::TooLarge;
    } else if (bounded.expired()) {
        result = FetchResult::TimedOut;
    } else {
        result = FetchResult::ParseError;
    }
    http.end(); // release fetch memory BEFORE rendering
    if (!ok) {
        Serial.printf("Stationboard fetch failed: %d\n", (int)result);
        return result;
    }
    const int view = displayMode == 0 ? 0 : 1;
    currentSnapshots[view] = snapshot;
    hasCurrentSnapshot[view] = true;
    drawStation(currentSnapshots[view].station);
    displayTransports(currentSnapshots[view]);
    return FetchResult::Success;
}
