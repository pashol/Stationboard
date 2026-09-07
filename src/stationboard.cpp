#include "stationboard.h"
#include "globals.h"
#include "utilities.h"
// #include "NotoSansBold15.h"
#include <HTTPClient.h>
#include <JsonStreamingParser.h>

TransportListener::TransportListener() : inStationboard(false), inStop(false) {}

const std::vector<Transport>& TransportListener::getTransports() const {
    return transports;
}

String TransportListener::getStation() const {
    return station;
}

void TransportListener::whitespace(char c) {
    // Ignore whitespace characters
}

void TransportListener::startDocument() {   
    Serial.println("Start parsing");
    transports.clear();
    station = "";
    inStationboard = false;
    currentPath = "";
    currentKey = "";
    inStop = false;
}

void TransportListener::key(String key) {
    currentKey = key;
    
    if (inStationboard) {
        if (inStop) {
            currentPath = "stationboard/stop/" + key;
        } else {
            currentPath = "stationboard/" + key;
        }
    }
}

void TransportListener::value(String value) {
    String fullPath = currentPath + "/" + currentKey;

    if (station.isEmpty()){
        if (fullPath == "/station/name") {
            station = value;
            Serial.print("Found station: ");
            Serial.println(value);
        }
    }

    if (fullPath.endsWith("/stop/departure")) {
        currentTransport.departure = extractTime(value);
    }
    else if (fullPath.endsWith("/stop/delay")) {
        currentTransport.delay = value;
    }
    else if (fullPath.endsWith("/name")) {
        currentTransport.name = value;
    }
    else if (fullPath.endsWith("/category")) {
        currentTransport.category = value;
    }
    else if (fullPath.endsWith("/number")) {
        if (value != "null") {
            int numValue = value.toInt();
            currentTransport.number = (numValue < 1000) ? String(numValue) : "";
        }
    }
    else if (fullPath.endsWith("/to")) {
        if (value.length() > 25) {
            value = value.substring(0, 22) + "...";
        }
        currentTransport.destination = value;
        transports.push_back(currentTransport);
        resetTransport();
    }
}

void TransportListener::endArray() {
    int lastSlash = currentPath.lastIndexOf('/');
    if (lastSlash >= 0) {
        currentPath = currentPath.substring(0, lastSlash);
    }
}

void TransportListener::startArray() {
    if (currentKey.length() > 0) {
        currentPath += "/" + currentKey;
    }
}

void TransportListener::startObject() {
    if (currentKey.length() > 0) {
        currentPath += "/" + currentKey;
    }
}

void TransportListener::endObject() {
    int lastSlash = currentPath.lastIndexOf('/');
    if (lastSlash >= 0) {
        currentPath = currentPath.substring(0, lastSlash);
    }
}

void TransportListener::endDocument() {
    Serial.println("Ende JSON");
    Serial.println("----------------------------");
}

void TransportListener::resetTransport() {
    currentTransport = Transport();
}

String TransportListener::extractTime(const String& isoTime) {
    if (isoTime.length() >= 16) {
        return isoTime.substring(11, 16); // Extract HH:MM
    }
    return "";
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
    size_t count = snapshot.count <= MAX_TRANSPORTS ? snapshot.count : MAX_TRANSPORTS;

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

bool drawStationboard() {
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT);
    http.useHTTP10(true);

    String currentStationId = (displayMode == 0) ? config.stationId : config.stationId2;

    String url = "http://transport.opendata.ch/v1/stationboard?id=" +
                    URLEncode(currentStationId) + "&limit=" + URLEncode(String(config.limit)) +"&datetime=" + URLEncode(getFormattedTimeRelativeToNow(config.offset));

    Serial.println("Relative Time: " + getFormattedTimeRelativeToNow(config.offset));
    Serial.print("URL: ");
    Serial.println(url);
    http.begin(url);

    if (http.GET() != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    // Reject a known oversized body before parsing; unknown/misreported
    // lengths stay bounded by the fixed 8KB document + filter (Task 7 adds
    // byte-counting on the stream itself).
    int contentLength = http.getSize();
    if (contentLength > (int)MAX_API_RESPONSE_BYTES) {
        Serial.printf("Stationboard response too large: %d bytes\n", contentLength);
        http.end();
        return false;
    }
    StationboardSnapshot snapshot;
    bool ok = parseStationboard(http.getStream(), snapshot);
    http.end(); // release fetch memory BEFORE rendering
    if (!ok) {
        return false; // leave display untouched (stale semantics = Task 8)
    }
    drawStation(snapshot.station);
    displayTransports(snapshot);
    return true;
}
