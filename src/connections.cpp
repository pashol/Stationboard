#include "connections.h"
#include "globals.h"
#include "utilities.h"
#include <HTTPClient.h>

// ── Display functions ────────────────────────────────────────────────────────

void drawConnectionsHeader(const String& from, const String& to) {
    tft.loadFont(AA_FONT_SMALL);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(0, 0, tft.width(), 25, TFT_WHITE);
    String label = from + " - " + to;
    if (label.length() > 30) label = label.substring(0, 27) + "...";
    tft.drawString(label, POS_BUS, 7);
}

void drawConnection(TFT_eSprite& sprite, const Connection& conn, int yPos) {
    const char* LONG_DISTANCE[] = {"IR", "IC", "EC", "ICE", "ICN", "TGV"};
    const char* REGIONAL[]      = {"S", "RE", "RB", "R", "T", "N", "SN"};

    // Determine product category from the product string (first word)
    String product = conn.product;
    product.trim();
    String category = "";
    int spaceIdx = product.indexOf(' ');
    if (spaceIdx > 0) category = product.substring(0, spaceIdx);
    else category = product;

    bool isLongDistance = false;
    for (auto& cat : LONG_DISTANCE) { if (category == cat) { isLongDistance = true; break; } }
    bool isRegional = false;
    for (auto& cat : REGIONAL) { if (category == cat) { isRegional = true; break; } }

    // Draw product badge (colored background, same as drawTransport)
    String productLabel = product;
    if (productLabel.length() > 6) productLabel = productLabel.substring(0, 6);
    while (productLabel.length() < 6) productLabel += " ";

    if (isLongDistance) {
        sprite.setTextColor(TFT_WHITE, TFT_RED);
        sprite.fillRect(0, yPos, POS_TIME - POS_BUS - 1, POS_INC - 3, TFT_RED);
    } else if (isRegional) {
        sprite.setTextColor(TFT_BLUE, TFT_WHITE);
        sprite.fillRect(0, yPos, POS_TIME - POS_BUS - 1, POS_INC - 3, TFT_WHITE);
    } else {
        sprite.setTextColor(TFT_WHITE, TFT_BLUE);
    }
    sprite.drawString(productLabel, POS_BUS, yPos + 1);

    // Departure time
    sprite.setTextColor(TFT_WHITE, TFT_BLUE);
    sprite.drawString(conn.departure, POS_TIME, yPos + 1);

    // Arrow
    sprite.drawString("-", 97, yPos + 1);

    // Arrival time
    sprite.drawString(conn.arrival, 115, yPos + 1);

    // Duration
    sprite.drawString(conn.duration, 168, yPos + 1);

    // Transfers
    sprite.drawString(String(conn.transfers) + "<>", 215, yPos + 1);

    // Delay (only if > 0)
    if (conn.delay > 0) {
        sprite.setTextColor(TFT_YELLOW, TFT_BLUE);
        sprite.drawString("+" + String(conn.delay), 260, yPos + 1);
    }
}

void displayConnections(const ConnectionsSnapshot& snapshot) {
    // No copies: rows are read in place. Loops are bounded to
    // snapshot.count (which never exceeds MAX_CONNECTIONS by the parser
    // contract), clamped defensively.
    size_t count = snapshot.count <= MAX_CONNECTIONS ? snapshot.count : MAX_CONNECTIONS;
    Serial.printf("Displaying %d connections\n", count);

    TFT_eSprite sprite(&tft);
    sprite.setColorDepth(8);
    if (sprite.createSprite(tft.width(), 5 * POS_INC) == nullptr) {
        Serial.println("displayConnections: sprite allocation failed - keeping previous frame");
        return;
    }
    sprite.loadFont(AA_FONT_SMALL);

    // Draw first half (0-4)
    sprite.fillSprite(TFT_BLUE);
    for (size_t i = 0; i < count && i < 5; i++) {
        drawConnection(sprite, snapshot.rows[i], i * POS_INC);
    }
    sprite.pushSprite(0, POS_FIRST);

    // Draw second half (5-7)
    sprite.fillSprite(TFT_BLUE);
    for (size_t i = 5; i < count; i++) {
        drawConnection(sprite, snapshot.rows[i], (i - 5) * POS_INC);
    }
    sprite.pushSprite(0, POS_FIRST + (5 * POS_INC));
}

bool fetchAndDrawConnections() {
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT);
    http.useHTTP10(true);

    String url = "http://transport.opendata.ch/v1/connections?from=" +
        URLEncode(config.stationId) + "&to=" + URLEncode(config.stationId2) + "&limit=8";

    Serial.print("Connections URL: ");
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
        Serial.printf("Connections response too large: %d bytes\n", contentLength);
        http.end();
        return false;
    }
    ConnectionsSnapshot snapshot;
    bool ok = parseConnections(http.getStream(), snapshot);
    http.end(); // release fetch memory BEFORE rendering
    if (!ok) {
        return false; // leave display untouched (stale semantics = Task 8)
    }
    drawConnectionsHeader(config.stationId, config.stationId2);
    displayConnections(snapshot);
    return true;
}
