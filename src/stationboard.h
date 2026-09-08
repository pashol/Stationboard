#ifndef STATIONBOARD_H
#define STATIONBOARD_H

#include <Arduino.h>
#include <array>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "globals.h"
#include "http_request.h"
#include "utilities.h"

struct Transport {
    String name;
    String number;
    String operatorName;
    String destination;
    String departure;
    String delay;
    String category;
};

// Bounded stationboard snapshot (Task 5). Header-inline (URLEncode
// precedent) so device tests — which link no src/*.cpp — exercise the
// REAL firmware code path.
struct StationboardSnapshot {
    String station;
    std::array<Transport, MAX_TRANSPORTS> rows;
    size_t count = 0;
    unsigned long receivedAt = 0;
};

// Unsigned subtraction remains correct when millis() rolls over.
inline bool isSnapshotFresh(unsigned long receivedAt, unsigned long now,
                            unsigned long maxAgeMs) {
    return (now - receivedAt) < maxAgeMs;
}

struct RefreshResult {
    FetchResult transport;
    FetchResult btc;
    unsigned long attemptedAt;
};

inline bool isTransportFreshResult(FetchResult transport) {
    return transport == FetchResult::Success;
}

inline String buildStationboardUrl(const String& stationId, int limit,
                                   const String& datetime) {
    return "https://transport.opendata.ch/v1/stationboard?id=" +
           URLEncode(stationId) + "&limit=" + URLEncode(String(limit)) +
           "&datetime=" + URLEncode(datetime) +
           "&fields[]=station/name&fields[]=stationboard/name"
           "&fields[]=stationboard/category&fields[]=stationboard/number"
           "&fields[]=stationboard/to&fields[]=stationboard/stop/departure"
           "&fields[]=stationboard/stop/delay";
}

// Parse a stationboard API response from a Stream into a snapshot.
// Uses a fixed STATIONBOARD_JSON_CAPACITY (8KB) document plus a filter so
// ignored subtrees (passList, location, platform, ...) never consume RAM.
// Contract: returns true on success (output fully assigned,
// receivedAt = millis()); returns false on JSON error or overflow with
// output left UNCHANGED (parsed into a local temp first).
// Row policy: rows missing required fields (`to` / `stop.departure`) are
// SKIPPED, not fatal — only document-level error/overflow fails the parse.
// Row count is capped at MAX_TRANSPORTS. No Unicode replacement:
// ArduinoJson decodes \uXXXX escapes natively.
inline bool parseStationboard(Stream& input, StationboardSnapshot& output) {
    DynamicJsonDocument filter(512);
    filter["station"]["name"] = true;
    filter["stationboard"][0]["name"] = true;
    filter["stationboard"][0]["category"] = true;
    filter["stationboard"][0]["number"] = true;
    filter["stationboard"][0]["to"] = true;
    filter["stationboard"][0]["stop"]["departure"] = true;
    filter["stationboard"][0]["stop"]["delay"] = true;

    DynamicJsonDocument doc(STATIONBOARD_JSON_CAPACITY);
    DeserializationError error =
        deserializeJson(doc, input, DeserializationOption::Filter(filter));
    if (error) {
        return false;
    }
    if (doc.overflowed()) {
        return false;
    }

    StationboardSnapshot tmp;
    tmp.station = doc["station"]["name"] | "";

    JsonArray board = doc["stationboard"].as<JsonArray>();
    for (JsonObject entry : board) {
        if (tmp.count >= MAX_TRANSPORTS) {
            break;
        }
        const char* to = entry["to"] | "";
        const char* departureIso = entry["stop"]["departure"] | "";
        if (to[0] == '\0' || departureIso[0] == '\0') {
            continue; // skip invalid rows; document itself stays valid
        }
        Transport& row = tmp.rows[tmp.count];
        row.name = entry["name"] | "";
        row.category = entry["category"] | "";
        if (entry["number"].isNull()) {
            row.number = "";
        } else {
            String numStr = entry["number"].as<String>();
            if (numStr == "null") {
                row.number = "";
            } else {
                int numValue = numStr.toInt();
                row.number = (numValue < 1000) ? String(numValue) : "";
            }
        }
        row.destination = String(to);
        String iso = String(departureIso);
        row.departure = (iso.length() >= 16) ? iso.substring(11, 16) : iso;
        if (entry["stop"]["delay"].isNull()) {
            row.delay = "";
        } else {
            row.delay = entry["stop"]["delay"].as<String>();
        }
        tmp.count++;
    }

    tmp.receivedAt = millis();
    output = tmp;
    return true;
}

void drawTransport(TFT_eSprite& sprite, const Transport& transport, int yPos);
void displayTransports(const StationboardSnapshot& snapshot);
void drawStation(const String& station);
FetchResult drawStationboard();
void expireStationboardIfStale(unsigned long now);

#endif // STATIONBOARD_H
