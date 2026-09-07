#include "connections.h"
#include "globals.h"
#include "utilities.h"
#include <HTTPClient.h>
#include <JsonStreamingParser.h>

ConnectionsListener::ConnectionsListener() {}

const std::vector<Connection>& ConnectionsListener::getConnections() const {
    return _connections;
}

void ConnectionsListener::whitespace(char c) {}

void ConnectionsListener::startDocument() {
    _connections.clear();
    _depth = 0;
    _inConnections = false;
    _context = "";
    _lastKey = "";
    _gotProduct = false;
    resetConnection();
}

void ConnectionsListener::endDocument() {
    Serial.println("Connections JSON parsed");
}

void ConnectionsListener::startObject() {
    _depth++;
}

void ConnectionsListener::endObject() {
    // NOTE: all depth checks run BEFORE _depth-- below
    if (_depth == 3 && _inConnections) {
        _connections.push_back(_current);
        resetConnection();
        _context = "";
        _gotProduct = false;
    }
    if (_depth == 4) {
        _context = "";  // leaving from/to/etc sub-object
    }
    _depth--;
}

void ConnectionsListener::startArray() {
    if (_depth == 1 && _lastKey == "connections") {
        _inConnections = true;
    }
    _depth++;
}

void ConnectionsListener::endArray() {
    _depth--;
    if (_depth == 1) {
        _inConnections = false;
    }
    if (_context == "products") {
        _context = "";
    }
}

void ConnectionsListener::key(String k) {
    _lastKey = k;
    if (_depth == 3 && _inConnections) {
        _context = k;
    }
}

void ConnectionsListener::value(String v) {
    if (!_inConnections) return;

    if (_depth == 3) {
        // Direct connection-level fields
        if (_lastKey == "duration")  _current.duration = parseDuration(v);
        if (_lastKey == "transfers") _current.transfers = v.toInt();
    }
    if (_depth == 4) {
        // Fields inside from{} / to{} / products[]
        if (_context == "from") {
            if (_lastKey == "departure") _current.departure = extractTime(v);
            if (_lastKey == "delay")     _current.delay = v.toInt();
        }
        if (_context == "to") {
            if (_lastKey == "arrival") _current.arrival = extractTime(v);
        }
        if (_context == "products" && !_gotProduct) {
            _current.product = v;  // first product in array, no preceding key
            _gotProduct = true;
        }
    }
}

void ConnectionsListener::resetConnection() {
    _current = Connection();
}

String ConnectionsListener::extractTime(const String& isoTime) {
    if (isoTime.length() >= 16) {
        return isoTime.substring(11, 16);  // Extract HH:MM
    }
    return "";
}

String ConnectionsListener::parseDuration(const String& dur) {
    // Format: "00d00:48:00" -> "48m" or "1h8m"
    int dPos = dur.indexOf('d');
    if (dPos < 0) return dur;
    String timePart = dur.substring(dPos + 1);  // "00:48:00"
    int colon1 = timePart.indexOf(':');
    if (colon1 < 0) return dur;
    int h = timePart.substring(0, colon1).toInt();
    int colon2 = timePart.indexOf(':', colon1 + 1);
    int m = (colon2 >= 0) ? timePart.substring(colon1 + 1, colon2).toInt()
                          : timePart.substring(colon1 + 1).toInt();
    if (h > 0) return String(h) + "h" + String(m) + "m";
    return String(m) + "m";
}

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

void displayConnections(const std::vector<Connection>& connections) {
    Serial.printf("Displaying %d connections\n", connections.size());

    TFT_eSprite sprite(&tft);
    sprite.setColorDepth(8);
    sprite.createSprite(tft.width(), 5 * POS_INC);
    sprite.loadFont(AA_FONT_SMALL);

    // Draw first half (0-4)
    sprite.fillSprite(TFT_BLUE);
    for (size_t i = 0; i < std::min(size_t(5), connections.size()); i++) {
        drawConnection(sprite, connections[i], i * POS_INC);
    }
    sprite.pushSprite(0, POS_FIRST);

    // Draw second half (5-9)
    sprite.fillSprite(TFT_BLUE);
    for (size_t i = 5; i < connections.size(); i++) {
        drawConnection(sprite, connections[i], (i - 5) * POS_INC);
    }
    sprite.pushSprite(0, POS_FIRST + (5 * POS_INC));
}

void fetchAndDrawConnections() {
    static ConnectionsListener listener;
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT);

    String url = "http://transport.opendata.ch/v1/connections?from=" +
        URLEncode(config.stationId) + "&to=" + URLEncode(config.stationId2) + "&limit=8";

    Serial.print("Connections URL: ");
    Serial.println(url);
    http.begin(url);

    if (http.GET() == HTTP_CODE_OK) {
        String response = http.getString();
        // Handle Swiss Unicode characters (same as stationboard)
        response.replace("\\u00fc", "ü");
        response.replace("\\u00f6", "ö");
        response.replace("\\u00e4", "ä");
        response.replace("\\u00dc", "Ü");
        response.replace("\\u00d6", "Ö");
        response.replace("\\u00c4", "Ä");
        response.replace("\\u00e9", "é");
        response.replace("\\u00e0", "à");
        response.replace("\\u00e8", "è");

        JsonStreamingParser parser;
        parser.setListener(&listener);
        for (char c : response) {
            parser.parse(c);
        }
        parser.reset();

        drawConnectionsHeader(config.stationId, config.stationId2);
        displayConnections(listener.getConnections());
    }
    http.end();
}
