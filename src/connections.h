#ifndef CONNECTIONS_H
#define CONNECTIONS_H

#include <Arduino.h>
#include <array>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "globals.h"

struct Connection {
    String departure;   // "08:14"
    String arrival;     // "09:02"
    String duration;    // "48m" or "1h8m"
    String product;     // "IC 1"
    int    transfers = 0;
    int    delay     = 0;
    long   departureTimestamp = 0;
};

// Bounded connections snapshot (Task 6). Header-inline (URLEncode
// precedent) so device tests — which link no src/*.cpp — exercise the
// REAL firmware code path.
struct ConnectionsSnapshot {
    std::array<Connection, MAX_CONNECTIONS> rows;
    size_t count = 0;
    unsigned long receivedAt = 0;
};

// Format an API duration ("00d00:48:00") as "48m"/"1h8m". Multi-day
// durations fold days into hours ("01d02:30:00" -> "26h30m").
// Unparseable input is returned unchanged.
inline String formatConnectionDuration(const String& dur) {
    int dPos = dur.indexOf('d');
    if (dPos < 0) return dur;
    long days = dur.substring(0, dPos).toInt();
    String timePart = dur.substring(dPos + 1);  // "00:48:00"
    int colon1 = timePart.indexOf(':');
    if (colon1 < 0) return dur;
    long h = timePart.substring(0, colon1).toInt();
    int colon2 = timePart.indexOf(':', colon1 + 1);
    long m = (colon2 >= 0) ? timePart.substring(colon1 + 1, colon2).toInt()
                           : timePart.substring(colon1 + 1).toInt();
    h += days * 24;
    if (h > 0) return String(h) + "h" + String(m) + "m";
    return String(m) + "m";
}

// Parse a connections API response from a Stream into a snapshot.
// Uses a fixed CONNECTIONS_JSON_CAPACITY (8KB) document plus a filter so
// ignored subtrees (sections, journey, passList, station, ...) never
// consume RAM.
// Contract: returns true on success (output fully assigned,
// receivedAt = millis()); returns false on JSON error or overflow with
// output left UNCHANGED (parsed into a local temp first).
// Row policy: walking-only entries (empty/missing `products` array) are
// SKIPPED, not fatal — only document-level error/overflow fails the
// parse, so an all-walking doc succeeds with count 0.
// Row count is capped at MAX_CONNECTIONS. No Unicode replacement:
// ArduinoJson decodes \uXXXX escapes natively.
inline bool parseConnections(Stream& input, ConnectionsSnapshot& output) {
    DynamicJsonDocument filter(512);
    filter["connections"][0]["from"]["departure"] = true;
    filter["connections"][0]["from"]["departureTimestamp"] = true;
    filter["connections"][0]["from"]["delay"] = true;
    filter["connections"][0]["to"]["arrival"] = true;
    filter["connections"][0]["duration"] = true;
    filter["connections"][0]["transfers"] = true;
    filter["connections"][0]["products"] = true;

    DynamicJsonDocument doc(CONNECTIONS_JSON_CAPACITY);
    DeserializationError error =
        deserializeJson(doc, input, DeserializationOption::Filter(filter));
    if (error) {
        return false;
    }
    if (doc.overflowed()) {
        return false;
    }

    ConnectionsSnapshot tmp;
    JsonArray conns = doc["connections"].as<JsonArray>();
    for (JsonObject entry : conns) {
        if (tmp.count >= MAX_CONNECTIONS) {
            break;
        }
        JsonVariant products = entry["products"];
        if (!products.is<JsonArray>() || products.as<JsonArray>().size() == 0) {
            continue; // walking-only entry; document itself stays valid
        }
        Connection& row = tmp.rows[tmp.count];
        String depIso = entry["from"]["departure"] | "";
        row.departure = (depIso.length() >= 16) ? depIso.substring(11, 16) : depIso;
        String arrIso = entry["to"]["arrival"] | "";
        row.arrival = (arrIso.length() >= 16) ? arrIso.substring(11, 16) : arrIso;
        String dur = entry["duration"] | "";
        row.duration = formatConnectionDuration(dur);
        row.product = products.as<JsonArray>()[0] | "";
        row.transfers = entry["transfers"] | 0;
        row.delay = entry["from"]["delay"] | 0;
        row.departureTimestamp = entry["from"]["departureTimestamp"] | 0L;
        tmp.count++;
    }

    tmp.receivedAt = millis();
    output = tmp;
    return true;
}

void drawConnectionsHeader(const String& from, const String& to);
void drawConnection(TFT_eSprite& sprite, const Connection& conn, int yPos);
void displayConnections(const ConnectionsSnapshot& snapshot);
bool fetchAndDrawConnections();

#endif // CONNECTIONS_H
