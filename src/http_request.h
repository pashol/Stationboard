#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include "globals.h"

// Bounded HTTP transactions (Task 7). Header-inline (URLEncode precedent)
// so device tests — which link no src/*.cpp — exercise the REAL firmware
// code path.
//
// Every network fetch is bounded in four dimensions:
//   - connect timeout  (HTTPClient::setConnectTimeout, HTTP_TIMEOUT)
//   - socket inactivity (HTTPClient::setTimeout, HTTP_TIMEOUT)
//   - total wall-clock  (BoundedStream deadline, HTTP_TOTAL_TIMEOUT)
//   - response size     (Content-Length pre-check + BoundedStream byte cap,
//                        MAX_API_RESPONSE_BYTES; a missing or dishonest
//                        Content-Length cannot bypass the cap because bytes
//                        are counted during the parse itself)

constexpr int HTTP_OK_STATUS = 200;

// Typed fetch outcome. stationboard/connections keep their bool return for
// now (true == Success) to stay source-compatible; full aggregation of
// these results into one refresh status is Task 8.
enum class FetchResult {
    Success,     // HTTP 200 + complete valid body
    HttpError,   // non-200 status or connection failure
    TooLarge,    // Content-Length or counted bytes exceeded the cap
    TimedOut,    // inactivity or total deadline expired mid-body
    ParseError,  // 200 + body that does not decode
    OutOfMemory  // reserved: allocation failure during fetch/parse
};

struct RequestLimits {
    size_t maxBytes;
    unsigned long inactivityMs;
    unsigned long totalMs;
};

// Single policy shared by all three APIs (transport stationboard,
// transport connections, Coinbase BTC).
inline RequestLimits defaultLimits() {
    return RequestLimits{MAX_API_RESPONSE_BYTES, HTTP_TIMEOUT, HTTP_TOTAL_TIMEOUT};
}

// Rollover-safe deadline predicate: unsigned subtraction stays correct
// across the millis() wrap every ~49.7 days.
inline bool isExpired(unsigned long start, unsigned long now, unsigned long limit) {
    return (now - start) >= limit;
}

inline bool isContentLengthAllowed(int contentLength, const RequestLimits& limits) {
    return contentLength < 0 || (size_t)contentLength <= limits.maxBytes;
}

// Stream wrapper enforcing the byte cap plus the inactivity/total
// deadlines. An optional EOF predicate distinguishes a closed HTTP/1.0
// connection from a temporary socket gap: available()==0 alone is never EOF.
// Past any bound, read() returns -1 and available() returns 0. Peek never
// advances the byte counter.
class BoundedStream : public Stream {
public:
    using Stream::readBytes;
    using EofPredicate = bool (*)(Stream&);
    using Clock = unsigned long (*)();
    using Wait = void (*)();

    BoundedStream(Stream& inner, const RequestLimits& limits,
                  unsigned long startMs = millis(), EofPredicate sourceEof = nullptr,
                  Clock clock = nullptr, Wait wait = nullptr)
        : _inner(inner), _limits(limits), _startMs(startMs), _lastProgressMs(startMs),
          _sourceEof(sourceEof), _clock(clock), _wait(wait) {}

    int available() override {
        if (_bytesRead >= _limits.maxBytes) return 0;
        if (expired()) return 0;
        int n = _inner.available();
        if (n <= 0) return 0;
        size_t remaining = _limits.maxBytes - _bytesRead;
        if ((size_t)n > remaining) return (int)remaining;
        return n;
    }

    int read() override {
        if (expired()) return -1;
        if (sourceEof()) {
            _eofReached = true;
            return -1;
        }
        if (_bytesRead >= _limits.maxBytes) {
            return probePastLimit();
        }
        int c = _inner.read();
        if (c < 0) return -1;
        _bytesRead++;
        _lastProgressMs = now();
        return c;
    }

    int peek() override {
        if (expired()) return -1;
        if (sourceEof()) {
            _eofReached = true;
            return -1;
        }
        if (_bytesRead >= _limits.maxBytes) return probePastLimit();
        return _inner.peek();
    }

    size_t readBytes(uint8_t* buffer, size_t length) override {
        size_t count = 0;
        while (count < length) {
            int c = read();
            if (c >= 0) {
                buffer[count++] = (uint8_t)c;
                continue;
            }
            if (_eofReached || _limitReached || expired()) break;
            wait();
        }
        return count;
    }

    size_t write(uint8_t) override { return 0; }
    void flush() override {}

    size_t bytesRead() const { return _bytesRead; }
    bool limitReached() const { return _limitReached; }
    bool eofReached() const { return _eofReached; }
    bool expired() const {
        return isExpired(_startMs, now(), _limits.totalMs) ||
               isExpired(_lastProgressMs, now(), _limits.inactivityMs);
    }

private:
    unsigned long now() const { return _clock != nullptr ? _clock() : millis(); }
    bool sourceEof() const { return _sourceEof != nullptr && _sourceEof(_inner); }
    int probePastLimit() {
        if (expired()) return -1;

        // The cap itself is valid. Consume exactly one more source byte only
        // to prove overflow; it is never exposed to the caller or counted.
        if (_inner.read() >= 0) {
            _limitReached = true;
        } else if (sourceEof()) {
            _eofReached = true;
        }
        return -1;
    }
    void wait() const {
        if (_wait != nullptr) {
            _wait();
        } else {
            delay(1);
        }
    }

    Stream& _inner;
    RequestLimits _limits;
    const unsigned long _startMs;
    unsigned long _lastProgressMs;
    EofPredicate _sourceEof;
    Clock _clock;
    Wait _wait;
    size_t _bytesRead = 0;
    bool _limitReached = false;
    bool _eofReached = false;
};

// HTTP/1.0 uses connection-close framing. Parsing may legitimately finish at
// the first JSON document, so drain the remainder to prove no oversized tail
// or stalled body was hidden after it.
inline bool consumeToEnd(BoundedStream& stream) {
    uint8_t buffer[128];
    while (!stream.eofReached() && !stream.limitReached() && !stream.expired()) {
        stream.readBytes(buffer, sizeof(buffer));
    }
    return stream.eofReached();
}

// BTC verdict (Task 7, Step 5): HTTP 200 alone is NOT success — only
// 200 + a fully valid price payload is. Anything else is failure.
inline FetchResult btcVerdict(int httpCode, bool payloadValid) {
    if (httpCode != HTTP_OK_STATUS) return FetchResult::HttpError;
    return payloadValid ? FetchResult::Success : FetchResult::ParseError;
}

// Parse a Coinbase spot payload ("{"data":{...,"amount":"97123.45"}}")
// into whole dollars. Returns false on any JSON error or missing amount
// and leaves `outPrice` untouched, so callers can fail closed.
inline bool parseBtcPrice(const String& payload, String& outPrice) {
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, payload)) return false;
    if (!doc.containsKey("data") || !doc["data"].containsKey("amount")) return false;
    const char* amount = doc["data"]["amount"].as<const char*>();
    if (amount == nullptr || *amount == '\0') return false;
    char* end = nullptr;
    float price = strtof(amount, &end);
    if (end == amount || *end != '\0' || !isfinite(price) || price < 0 || price > INT_MAX) return false;
    outPrice = String((int)price);
    return true;
}

#endif // HTTP_REQUEST_H
