#ifndef STATIONBOARD_H
#define STATIONBOARD_H

#include <Arduino.h>
#include <vector>
#include <JsonListener.h>
#include <TFT_eSPI.h>

struct Transport {
    String name;
    String number;
    String operatorName;
    String destination;
    String departure;
    String delay;
    String category;
};

class TransportListener: public JsonListener {
public:
    TransportListener();
    const std::vector<Transport>& getTransports() const;
    String getStation() const;
    virtual void whitespace(char c);
    void startDocument();
    void key(String key);
    void value(String value);
    void endArray();
    void startArray();
    void startObject();
    void endObject();
    void endDocument();

private:
    String currentKey;
    String currentPath;
    String station;
    bool inStationboard;
    bool inStop;
    std::vector<Transport> transports;
    Transport currentTransport;

    void resetTransport();
    String extractTime(const String& isoTime);
};

void drawTransport(TFT_eSprite& sprite, const Transport& transport, int yPos);
void displayTransports(const std::vector<Transport>& transports);
void drawStation(const String& station);
void drawStationboard();

#endif // STATIONBOARD_H
