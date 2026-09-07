#ifndef CONNECTIONS_H
#define CONNECTIONS_H

#include <Arduino.h>
#include <JsonStreamingParser.h>
#include <JsonListener.h>
#include <TFT_eSPI.h>
#include <vector>

struct Connection {
    String departure;   // "08:14"
    String arrival;     // "09:02"
    String duration;    // "48m" or "1h8m"
    String product;     // "IC 1"
    int    transfers = 0;
    int    delay     = 0;
};

class ConnectionsListener : public JsonListener {
public:
    ConnectionsListener();
    void startDocument() override;
    void endDocument() override;
    void startObject() override;
    void endObject() override;
    void startArray() override;
    void endArray() override;
    void key(String key) override;
    void value(String value) override;
    void whitespace(char c) override;

    const std::vector<Connection>& getConnections() const;

private:
    std::vector<Connection> _connections;
    Connection _current;
    int    _depth = 0;
    bool   _inConnections = false;
    String _context;        // "from", "to", "products", etc. at depth 3
    String _lastKey;
    bool   _gotProduct = false;

    String extractTime(const String& isoTime);
    String parseDuration(const String& dur);
    void resetConnection();
};

void drawConnectionsHeader(const String& from, const String& to);
void drawConnection(TFT_eSprite& sprite, const Connection& conn, int yPos);
void displayConnections(const std::vector<Connection>& connections);
void fetchAndDrawConnections();

#endif // CONNECTIONS_H
