#include <Arduino.h>
#include <unity.h>
#include <SPIFFS.h>
#include <FS.h>
#include <Timezone.h>
#include "globals.h"
#include "utilities.h"
#include "networking.h"
#include "stationboard.h"
#include "connections.h"
#include "http_request.h"
#include "ota.h"

// Test-local definition: pio test links only this TU + libs (src/*.cpp is
// not linked), so the `config` global referenced by the header-inline
// persistence core is provided here. Firmware links globals.cpp instead.
Config config;

// Test-local HTTP_TIMEOUT: defaultLimits() is header-inline and reads the
// extern connect/inactivity timeout; firmware links globals.cpp instead
// (Task 7). Value must match globals.cpp (10000ms).
const unsigned long HTTP_TIMEOUT = 10000;

// Test-local portal owner for the same reason: the firmware's `wm` from
// globals.cpp is unavailable here. This instance lets the lifetime probe
// exercise the identical getPortalParameter() lookup.
WiFiManager wm;

void test_operational_limits_are_bounded() {
    TEST_ASSERT_EQUAL_UINT32(10, MAX_TRANSPORTS);
    TEST_ASSERT_EQUAL_UINT32(8, MAX_CONNECTIONS);
    TEST_ASSERT_EQUAL_UINT32(65536, MAX_API_RESPONSE_BYTES);
    TEST_ASSERT_EQUAL_UINT32(8192, STATIONBOARD_JSON_CAPACITY);
    TEST_ASSERT_EQUAL_UINT32(8192, CONNECTIONS_JSON_CAPACITY);
}

void test_url_encode_handles_utf8_bytes() {
    TEST_ASSERT_EQUAL_STRING("Z%C3%BCrich%20HB", URLEncode("Zürich HB").c_str());
}

void test_stationboard_url_requests_only_parsed_fields() {
    String url = buildStationboardUrl("Zürich HB", 10, "2026-09-08 12:34");
    TEST_ASSERT_EQUAL_STRING(
        "https://transport.opendata.ch/v1/stationboard?id=Z%C3%BCrich%20HB&limit=10&datetime=2026-09-08%2012%3A34"
        "&fields[]=station/name&fields[]=stationboard/name&fields[]=stationboard/category"
        "&fields[]=stationboard/number&fields[]=stationboard/to&fields[]=stationboard/stop/departure"
        "&fields[]=stationboard/stop/delay",
        url.c_str());
}

void test_config_rejects_empty_stations() {
    Config candidate;
    candidate.stationId = "";
    TEST_ASSERT_FALSE(validateConfiguration(candidate));
}

void test_config_clamps_numeric_ranges() {
    Config candidate;
    candidate.limit = 99;
    candidate.defaultBrightness = -3;
    candidate.offset = 100000;
    normalizeConfiguration(candidate);
    TEST_ASSERT_EQUAL(10, candidate.limit);
    TEST_ASSERT_EQUAL(0, candidate.defaultBrightness);
    TEST_ASSERT_EQUAL(MAX_STATION_OFFSET_MINUTES, candidate.offset);
}

void test_equal_night_times_disable_schedule() {
    Config candidate;
    candidate.nightModeEnabled = true;
    candidate.nightModeStartHour = candidate.nightModeEndHour = 22;
    candidate.nightModeStartMinute = candidate.nightModeEndMinute = 0;
    normalizeConfiguration(candidate);
    TEST_ASSERT_FALSE(candidate.nightModeEnabled);
}

// --- Task 9: nonblocking WiFi reconnect scheduling ---

void test_reconnect_state_is_due_initially() {
    ReconnectState state;
    TEST_ASSERT_TRUE(reconnectDue(state, 0));
}

void test_reconnect_failures_use_capped_exponential_backoff() {
    ReconnectState state;
    recordReconnectFailure(state, 100);
    TEST_ASSERT_EQUAL_UINT32(1100, state.nextAttemptAt);
    TEST_ASSERT_EQUAL_UINT32(2000, state.backoffMs);
    TEST_ASSERT_TRUE(state.attemptScheduled);

    recordReconnectFailure(state, 1100);
    TEST_ASSERT_EQUAL_UINT32(3100, state.nextAttemptAt);
    TEST_ASSERT_EQUAL_UINT32(4000, state.backoffMs);

    state.backoffMs = WIFI_RETRY_MAX_MS;
    recordReconnectFailure(state, 5000);
    TEST_ASSERT_EQUAL_UINT32(5000 + WIFI_RETRY_MAX_MS, state.nextAttemptAt);
    TEST_ASSERT_EQUAL_UINT32(WIFI_RETRY_MAX_MS, state.backoffMs);
}

void test_reconnect_due_handles_millis_rollover() {
    ReconnectState state;
    state.nextAttemptAt = 20;
    state.attemptScheduled = true;
    TEST_ASSERT_FALSE(reconnectDue(state, ULONG_MAX - 10));
    TEST_ASSERT_TRUE(reconnectDue(state, 20));
}

void test_reconnect_due_handles_deadline_wrapping_to_zero() {
    ReconnectState state;
    const unsigned long initialNow = ULONG_MAX - 999;
    recordReconnectFailure(state, initialNow);

    TEST_ASSERT_EQUAL_UINT32(0, state.nextAttemptAt);
    TEST_ASSERT_FALSE(reconnectDue(state, initialNow));
    TEST_ASSERT_TRUE(reconnectDue(state, 0));
}

void test_reconnect_success_resets_scheduler() {
    ReconnectState state;
    state.nextAttemptAt = 12345;
    state.backoffMs = 4000;
    recordReconnectSuccess(state);
    TEST_ASSERT_EQUAL_UINT32(0, state.nextAttemptAt);
    TEST_ASSERT_EQUAL_UINT32(WIFI_RETRY_INITIAL_MS, state.backoffMs);
    TEST_ASSERT_FALSE(state.attemptScheduled);
}

void test_refresh_attempt_interval_handles_failed_attempts_and_rollover() {
    TEST_ASSERT_FALSE(shouldAttemptRefresh(1000, 1500, 1000));
    TEST_ASSERT_TRUE(shouldAttemptRefresh(1000, 2000, 1000));
    TEST_ASSERT_TRUE(shouldAttemptRefresh(ULONG_MAX - 500, 499, 1000));
}

// --- Task 10: clock validity and fail-safe night scheduling ---

void test_plausible_epoch_requires_2024_or_later() {
    TEST_ASSERT_FALSE(isPlausibleEpoch(0));
    TEST_ASSERT_FALSE(isPlausibleEpoch((time_t)1704067199));
    TEST_ASSERT_TRUE(isPlausibleEpoch((time_t)1704067200));
}

void test_failed_clock_sync_keeps_an_invalid_clock_invalid() {
    TEST_ASSERT_FALSE(clockValidityAfterSync(false, false, PLAUSIBLE_EPOCH_START, false));
    TEST_ASSERT_FALSE(clockValidityAfterSync(false, true, PLAUSIBLE_EPOCH_START - 1, false));
    TEST_ASSERT_FALSE(clockValidityAfterSync(false, true, PLAUSIBLE_EPOCH_START, false));
    TEST_ASSERT_TRUE(clockValidityAfterSync(false, true, PLAUSIBLE_EPOCH_START, true));
}

void test_epoch_timeval_conversion_has_zero_microseconds() {
    const time_t epoch = (time_t)1774746000;
    const timeval timeValue = timevalFromEpoch(epoch);

    TEST_ASSERT_EQUAL_INT64(epoch, timeValue.tv_sec);
    TEST_ASSERT_EQUAL_INT(0, timeValue.tv_usec);
}

void test_invalid_clock_disables_night_mode() {
    TEST_ASSERT_FALSE(isNightModeEligible(false, true));
    TEST_ASSERT_FALSE(isNightModeEligible(true, false));
    TEST_ASSERT_TRUE(isNightModeEligible(true, true));
}

void test_night_schedule_handles_overnight_boundaries() {
    const int start = 22 * 60;
    const int end = 7 * 60;
    TEST_ASSERT_FALSE(isNightScheduleActive(21 * 60 + 59, start, end));
    TEST_ASSERT_TRUE(isNightScheduleActive(22 * 60, start, end));
    TEST_ASSERT_TRUE(isNightScheduleActive(6 * 60 + 59, start, end));
    TEST_ASSERT_FALSE(isNightScheduleActive(7 * 60, start, end));
    TEST_ASSERT_FALSE(isNightScheduleActive(22 * 60, start, start));
}

void test_night_schedule_uses_local_time_across_dst_boundaries() {
    TimeChangeRule cest = {"CEST", Last, Sun, Mar, 2, 120};
    TimeChangeRule cet = {"CET ", Last, Sun, Oct, 3, 60};
    Timezone europeZurich(cest, cet);

    const time_t beforeSpringForward = 1774745940; // 2026-03-29 00:59 UTC
    const time_t afterSpringForward = 1774746000;  // 2026-03-29 01:00 UTC
    const time_t beforeFallBack = 1792889940;      // 2026-10-25 00:59 UTC
    const time_t afterFallBack = 1792890000;       // 2026-10-25 01:00 UTC

    TEST_ASSERT_EQUAL(119, hour(europeZurich.toLocal(beforeSpringForward)) * 60 +
                               minute(europeZurich.toLocal(beforeSpringForward)));
    TEST_ASSERT_EQUAL(180, hour(europeZurich.toLocal(afterSpringForward)) * 60 +
                               minute(europeZurich.toLocal(afterSpringForward)));
    TEST_ASSERT_EQUAL(179, hour(europeZurich.toLocal(beforeFallBack)) * 60 +
                               minute(europeZurich.toLocal(beforeFallBack)));
    TEST_ASSERT_EQUAL(120, hour(europeZurich.toLocal(afterFallBack)) * 60 +
                               minute(europeZurich.toLocal(afterFallBack)));
}

void test_weekend_disablement_overrides_an_active_night_schedule() {
    const int start = 22 * 60;
    const int end = 7 * 60;
    TEST_ASSERT_FALSE(isNightModeActiveAtLocalTime(true, true, true, true, 23 * 60,
                                                    start, end));
    TEST_ASSERT_TRUE(isNightModeActiveAtLocalTime(true, true, true, false, 23 * 60,
                                                   start, end));
}

void test_clock_retry_interval_handles_millis_rollover() {
    TEST_ASSERT_FALSE(clockRetryDue(1000, 1000 + CLOCK_RETRY_INTERVAL - 1));
    TEST_ASSERT_TRUE(clockRetryDue(1000, 1000 + CLOCK_RETRY_INTERVAL));
    TEST_ASSERT_TRUE(clockRetryDue(ULONG_MAX - 30000, 29999));
}

void test_temporary_wake_expiry_handles_millis_rollover() {
    const unsigned long duration = 30000;
    TEST_ASSERT_FALSE(temporaryWakeExpired(1000, 1000 + duration - 1, duration));
    TEST_ASSERT_TRUE(temporaryWakeExpired(1000, 1000 + duration, duration));
    TEST_ASSERT_FALSE(temporaryWakeExpired(ULONG_MAX - 10000, 19998, duration));
    TEST_ASSERT_TRUE(temporaryWakeExpired(ULONG_MAX - 10000, 19999, duration));
}

void test_wake_policy_ignores_wifi_and_unknown_wakes() {
    TEST_ASSERT_EQUAL(static_cast<int>(WakeAction::Timer),
                      static_cast<int>(wakeActionFor(WakeSource::Timer)));
    TEST_ASSERT_EQUAL(static_cast<int>(WakeAction::Button),
                      static_cast<int>(wakeActionFor(WakeSource::Ext0)));
    TEST_ASSERT_EQUAL(static_cast<int>(WakeAction::Ignore),
                      static_cast<int>(wakeActionFor(WakeSource::WiFi)));
    TEST_ASSERT_EQUAL(static_cast<int>(WakeAction::Ignore),
                      static_cast<int>(wakeActionFor(WakeSource::Other)));
}

void setUp(void) {}
void tearDown(void) {}

// --- Task 3: transactional config persistence (SPIFFS) ---

static void writeTestFile(const char* path, const char* content) {
    fs::File f = SPIFFS.open(path, FILE_WRITE);
    TEST_ASSERT_TRUE_MESSAGE(f, "open test file for write");
    size_t n = f.print(content);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, (int)n, "write test file bytes");
    f.close();
}

static void clearConfigFiles() {
    if (SPIFFS.exists(CONFIG_PATH)) SPIFFS.remove(CONFIG_PATH);
    if (SPIFFS.exists(CONFIG_TEMP_PATH)) SPIFFS.remove(CONFIG_TEMP_PATH);
    if (SPIFFS.exists(CONFIG_BACKUP_PATH)) SPIFFS.remove(CONFIG_BACKUP_PATH);
}

static const char* VALID_PRIMARY_JSON =
    "{\"station_id\":\"Bern\",\"station_id2\":\"Thun\",\"limit\":5,\"offset\":10,"
    "\"defaultBrightness\":2,\"nightModeEnabled\":false,"
    "\"nightModeStartHour\":22,\"nightModeStartMinute\":0,"
    "\"nightModeEndHour\":7,\"nightModeEndMinute\":0,"
    "\"nightModeWeekendDisable\":false,\"connectionsEnabled\":false}";

static const char* VALID_BACKUP_JSON =
    "{\"station_id\":\"Luzern\",\"station_id2\":\"Zug\",\"limit\":8,\"offset\":0,"
    "\"defaultBrightness\":4,\"nightModeEnabled\":false,"
    "\"nightModeStartHour\":22,\"nightModeStartMinute\":0,"
    "\"nightModeEndHour\":7,\"nightModeEndMinute\":0,"
    "\"nightModeWeekendDisable\":false,\"connectionsEnabled\":false}";

void test_valid_primary_load_keeps_values() {
    clearConfigFiles();
    writeTestFile(CONFIG_PATH, VALID_PRIMARY_JSON);
    config = Config();
    config.stationId = "SENTINEL";
    bool ok = loadConfiguration();
    TEST_ASSERT_TRUE_MESSAGE(ok, "valid primary should load");
    TEST_ASSERT_EQUAL_STRING("Bern", config.stationId.c_str());
    TEST_ASSERT_EQUAL_STRING("Thun", config.stationId2.c_str());
    TEST_ASSERT_EQUAL_INT(5, config.limit);
    clearConfigFiles();
}

void test_corrupt_primary_valid_backup_recovers_backup_values() {
    clearConfigFiles();
    writeTestFile(CONFIG_PATH, "{not valid json!!!");
    writeTestFile(CONFIG_BACKUP_PATH, VALID_BACKUP_JSON);
    config = Config();
    config.stationId = "SENTINEL";
    bool ok = loadConfiguration();
    TEST_ASSERT_TRUE_MESSAGE(ok, "valid backup should recover");
    TEST_ASSERT_EQUAL_STRING("Luzern", config.stationId.c_str());
    TEST_ASSERT_EQUAL_STRING("Zug", config.stationId2.c_str());
    TEST_ASSERT_EQUAL_INT(8, config.limit);
    clearConfigFiles();
}

void test_incomplete_temp_file_is_rejected() {
    clearConfigFiles();
    // Truncated JSON must never be assigned to the live config.
    writeTestFile(CONFIG_TEMP_PATH, "{\"station_id\":\"Bern\",");
    config = Config();
    config.stationId = "SENTINEL";
    config.stationId2 = "SENTINEL2";
    bool ok = loadConfiguration();
    TEST_ASSERT_FALSE_MESSAGE(ok, "temp-only truncated file must not load");
    TEST_ASSERT_EQUAL_STRING("SENTINEL", config.stationId.c_str());
    TEST_ASSERT_EQUAL_STRING("SENTINEL2", config.stationId2.c_str());
    clearConfigFiles();
}

void test_save_returns_true_on_success() {
    clearConfigFiles();
    config = Config();
    config.stationId = "Bern";
    config.stationId2 = "Thun";
    config.limit = 5;
    bool ok = saveConfiguration();
    TEST_ASSERT_TRUE_MESSAGE(ok, "save should succeed");
    TEST_ASSERT_TRUE_MESSAGE(SPIFFS.exists(CONFIG_PATH), "primary should exist after save");
    clearConfigFiles();
}

void test_double_save_rotates_backup() {
    clearConfigFiles();
    // First save establishes the primary.
    config = Config();
    config.stationId = "Bern";
    config.stationId2 = "Thun";
    config.limit = 5;
    TEST_ASSERT_TRUE_MESSAGE(saveConfiguration(), "first save should succeed");
    TEST_ASSERT_TRUE_MESSAGE(SPIFFS.exists(CONFIG_PATH), "primary should exist after first save");
    // Second save with different stations must rotate the prior primary to backup.
    config.stationId = "Luzern";
    config.stationId2 = "Zug";
    config.limit = 8;
    TEST_ASSERT_TRUE_MESSAGE(saveConfiguration(), "second save should succeed");
    TEST_ASSERT_TRUE_MESSAGE(SPIFFS.exists(CONFIG_BACKUP_PATH), "backup should exist after second save");
    // Backup must hold the first stations (rotation mechanics for restore path steps 4/5/7).
    config = Config();
    config.stationId = "SENTINEL";
    Config recovered;
    TEST_ASSERT_TRUE_MESSAGE(parseValidateConfigFile(CONFIG_BACKUP_PATH, recovered), "backup should parse");
    TEST_ASSERT_EQUAL_STRING("Bern", recovered.stationId.c_str());
    TEST_ASSERT_EQUAL_STRING("Thun", recovered.stationId2.c_str());
    TEST_ASSERT_EQUAL_INT(5, recovered.limit);
    // Primary must hold the second stations.
    Config primary;
    TEST_ASSERT_TRUE_MESSAGE(parseValidateConfigFile(CONFIG_PATH, primary), "primary should parse");
    TEST_ASSERT_EQUAL_STRING("Luzern", primary.stationId.c_str());
    TEST_ASSERT_EQUAL_STRING("Zug", primary.stationId2.c_str());
    TEST_ASSERT_EQUAL_INT(8, primary.limit);
    clearConfigFiles();
}

// --- Task 4: portal parameter lifetime probe ---
//
// pio test links only this TU (networking.cpp is NOT linked), so this probe
// cannot observe the firmware's own parameter addresses directly. Lookup
// stability over MULTIPLE params is asserted here; firmware-side stability
// itself is enforced structurally (anonymous-namespace statics in
// networking.cpp) and verified at runtime by portalParametersAreStable().

namespace {
WiFiManagerParameter probeStationParam("station", "Station ID 1", "", 150);
WiFiManagerParameter probeStation2Param("station2", "Station ID 2", "", 150);
WiFiManagerParameter probeLimitParam("limit", "Number of Entries", "", 2);
bool probeParamsRegistered = false;
}

void test_portal_parameters_have_program_lifetime() {
    if (!probeParamsRegistered) {
        TEST_ASSERT_TRUE_MESSAGE(wm.addParameter(&probeStationParam),
                                 "register station probe param");
        TEST_ASSERT_TRUE_MESSAGE(wm.addParameter(&probeStation2Param),
                                 "register station2 probe param");
        TEST_ASSERT_TRUE_MESSAGE(wm.addParameter(&probeLimitParam),
                                 "register limit probe param");
        probeParamsRegistered = true;
    }
    const char* ids[3] = {"station", "station2", "limit"};
    for (int i = 0; i < 3; i++) {
        WiFiManagerParameter* first = getPortalParameter(ids[i]);
        WiFiManagerParameter* second = getPortalParameter(ids[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(first, "probe param must resolve");
        TEST_ASSERT_EQUAL_PTR_MESSAGE(first, second,
                                      "portal param address must be stable across lookups");
    }
    TEST_ASSERT_EQUAL_PTR_MESSAGE(getPortalParameter("station"),
                                  getPortalParameter("station"),
                                  "station lookup must be repeatable");
    TEST_ASSERT_TRUE_MESSAGE(getPortalParameter("station") != getPortalParameter("station2"),
                             "distinct ids must resolve to distinct params");
    TEST_ASSERT_TRUE_MESSAGE(getPortalParameter("no-such-param") == nullptr,
                             "unknown id must return nullptr");
}

// --- Task 5: bounded stationboard snapshot parser ---
//
// parseStationboard() is header-inline in stationboard.h (URLEncode
// precedent) because pio test links only this TU — src/*.cpp is NOT
// linked — so tests exercise the REAL firmware code path.
// Contract: true on success (output fully assigned, receivedAt = millis());
// false on JSON error/overflow with output left UNCHANGED. Rows missing
// required fields (to / stop.departure) are skipped, not fatal.

// Minimal Stream adapter over an in-memory String (avoids depending on
// StreamString availability in the test core).
class StringStream : public Stream {
public:
    explicit StringStream(const String& s) : data(&s), pos(0) {}
    explicit StringStream(const char* s) : owned(s), data(&owned), pos(0) {}
    int available() override { return (int)data->length() - (int)pos; }
    int read() override {
        if (pos >= data->length()) return -1;
        return (unsigned char)(*data)[pos++];
    }
    int peek() override {
        if (pos >= data->length()) return -1;
        return (unsigned char)(*data)[pos];
    }
    size_t write(uint8_t) override { return 0; }
    bool eof() const { return pos >= data->length(); }
private:
    String owned;
    const String* data;
    size_t pos;
};

static bool parseFromString(const String& json, StationboardSnapshot& snap) {
    StringStream stream(json);
    return parseStationboard(stream, snap);
}

static const char* SB_VALID_JSON =
    "{\"station\":{\"name\":\"Bern\"},\"stationboard\":["
    "{\"name\":\"IC 1\",\"category\":\"IC\",\"number\":\"1\","
    "\"to\":\"Zurich HB\","
    "\"stop\":{\"departure\":\"2026-09-07T12:34:00+0200\",\"delay\":null}},"
    "{\"name\":\"S 2\",\"category\":\"S\",\"number\":\"2\","
    "\"to\":\"Thun\","
    "\"stop\":{\"departure\":\"2026-09-07T12:41:00+0200\",\"delay\":\"3\"}}"
    "]}";

void test_sb_valid_doc_parses() {
    StationboardSnapshot snap;
    unsigned long before = millis();
    TEST_ASSERT_TRUE_MESSAGE(parseFromString(SB_VALID_JSON, snap), "valid doc must parse");
    TEST_ASSERT_EQUAL_STRING("Bern", snap.station.c_str());
    TEST_ASSERT_EQUAL_UINT(2, snap.count);
    TEST_ASSERT_EQUAL_STRING("Zurich HB", snap.rows[0].destination.c_str());
    TEST_ASSERT_EQUAL_STRING("12:34", snap.rows[0].departure.c_str());
    TEST_ASSERT_EQUAL_STRING("IC", snap.rows[0].category.c_str());
    TEST_ASSERT_EQUAL_STRING("1", snap.rows[0].number.c_str());
    TEST_ASSERT_EQUAL_STRING("Thun", snap.rows[1].destination.c_str());
    TEST_ASSERT_EQUAL_STRING("12:41", snap.rows[1].departure.c_str());
    TEST_ASSERT_EQUAL_STRING("3", snap.rows[1].delay.c_str());
    TEST_ASSERT_TRUE_MESSAGE(snap.receivedAt >= before, "receivedAt must be stamped on success");
}

static const char* SB_REORDERED_JSON =
    "{\"stationboard\":["
    "{\"to\":\"Thun\","
    "\"stop\":{\"delay\":\"3\",\"departure\":\"2026-09-07T12:41:00+0200\"},"
    "\"number\":\"2\",\"category\":\"S\",\"name\":\"S 2\"}"
    "],\"station\":{\"name\":\"Bern\"}}";

void test_sb_reordered_members_parse() {
    StationboardSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseFromString(SB_REORDERED_JSON, snap), "reordered doc must parse");
    TEST_ASSERT_EQUAL_STRING("Bern", snap.station.c_str());
    TEST_ASSERT_EQUAL_UINT(1, snap.count);
    TEST_ASSERT_EQUAL_STRING("Thun", snap.rows[0].destination.c_str());
    TEST_ASSERT_EQUAL_STRING("12:41", snap.rows[0].departure.c_str());
}

static const char* SB_NESTED_JSON =
    "{\"station\":{\"name\":\"Bern\",\"location\":{\"name\":\"FAKE\",\"to\":\"FAKE\"}},"
    "\"stationboard\":["
    "{\"name\":\"IC 1\",\"category\":\"IC\",\"number\":\"1\","
    "\"to\":\"Zurich HB\",\"passList\":[{\"name\":\"FAKE\",\"to\":\"FAKE\"}],"
    "\"stop\":{\"departure\":\"2026-09-07T12:34:00+0200\",\"departureTimestamp\":123,"
    "\"delay\":null,\"platform\":\"FAKE\"}}"
    "]}";

void test_sb_nested_unrelated_keys_ignored() {
    StationboardSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseFromString(SB_NESTED_JSON, snap), "nested doc must parse");
    TEST_ASSERT_EQUAL_STRING("Bern", snap.station.c_str());
    TEST_ASSERT_EQUAL_UINT(1, snap.count);
    TEST_ASSERT_EQUAL_STRING("Zurich HB", snap.rows[0].destination.c_str());
    TEST_ASSERT_EQUAL_STRING("12:34", snap.rows[0].departure.c_str());
}

void test_sb_truncated_fails_and_preserves_output() {
    String truncated = String(SB_VALID_JSON).substring(0, 120); // cut mid-document
    StationboardSnapshot snap;
    snap.station = "SENTINEL";
    snap.count = 2;
    snap.receivedAt = 12345;
    snap.rows[0].destination = "KEEP0";
    snap.rows[1].destination = "KEEP1";
    TEST_ASSERT_FALSE_MESSAGE(parseFromString(truncated, snap), "truncated doc must fail");
    TEST_ASSERT_EQUAL_STRING("SENTINEL", snap.station.c_str());
    TEST_ASSERT_EQUAL_UINT(2, snap.count);
    TEST_ASSERT_EQUAL_UINT32(12345, snap.receivedAt);
    TEST_ASSERT_EQUAL_STRING("KEEP0", snap.rows[0].destination.c_str());
    TEST_ASSERT_EQUAL_STRING("KEEP1", snap.rows[1].destination.c_str());
}

static String buildManyEntriesJson(int n) {
    String s = "{\"station\":{\"name\":\"Bern\"},\"stationboard\":[";
    for (int i = 0; i < n; i++) {
        if (i > 0) s += ",";
        s += "{\"name\":\"S ";
        s += String(i);
        s += "\",\"category\":\"S\",\"number\":\"";
        s += String(i);
        s += "\",\"to\":\"Dest";
        s += String(i);
        s += "\",\"stop\":{\"departure\":\"2026-09-07T12:34:00+0200\",\"delay\":null}}";
    }
    s += "]}";
    return s;
}

void test_sb_more_than_ten_entries_capped() {
    StationboardSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseFromString(buildManyEntriesJson(12), snap), "12-entry doc must parse");
    TEST_ASSERT_EQUAL_UINT(MAX_TRANSPORTS, snap.count);
    TEST_ASSERT_EQUAL_STRING("Dest0", snap.rows[0].destination.c_str());
    TEST_ASSERT_EQUAL_STRING("Dest9", snap.rows[9].destination.c_str());
}

static const char* SB_UNICODE_JSON =
    "{\"station\":{\"name\":\"Z\\u00fcrich HB\"},\"stationboard\":["
    "{\"name\":\"S 1\",\"category\":\"S\",\"number\":\"1\","
    "\"to\":\"Z\\u00fcrich HB\","
    "\"stop\":{\"departure\":\"2026-09-07T12:34:00+0200\",\"delay\":null}}"
    "]}";

void test_sb_unicode_escapes_decoded() {
    StationboardSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseFromString(SB_UNICODE_JSON, snap), "unicode doc must parse");
    TEST_ASSERT_EQUAL_STRING("Zürich HB", snap.station.c_str());
    TEST_ASSERT_EQUAL_STRING("Zürich HB", snap.rows[0].destination.c_str());
}

static String buildLargePassListJson() {
    // ~6KB of ignored passList payload: the filter must exclude it so the
    // 8KB document budget still succeeds.
    String s = "{\"station\":{\"name\":\"Bern\"},\"stationboard\":["
               "{\"name\":\"IC 1\",\"category\":\"IC\",\"number\":\"1\","
               "\"to\":\"Zurich HB\",\"passList\":[";
    for (int i = 0; i < 150; i++) {
        if (i > 0) s += ",";
        s += "{\"station\":{\"name\":\"PaddingStationNumber";
        s += String(i);
        s += "WithExtraTextToGrowThePayload\"},\"departure\":\"2026-09-07T12:34:00+0200\"}";
    }
    s += "],\"stop\":{\"departure\":\"2026-09-07T12:34:00+0200\",\"delay\":null}}]}";
    return s;
}

void test_sb_large_ignored_passlist_succeeds() {
    String json = buildLargePassListJson();
    TEST_ASSERT_TRUE_MESSAGE(json.length() > 6000, "fixture must carry a large ignored payload");
    StationboardSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseFromString(json, snap), "large ignored passList must succeed");
    TEST_ASSERT_EQUAL_STRING("Bern", snap.station.c_str());
    TEST_ASSERT_EQUAL_UINT(1, snap.count);
    TEST_ASSERT_EQUAL_STRING("Zurich HB", snap.rows[0].destination.c_str());
}

static String buildOver32KiBPassListJson() {
    String s = "{\"station\":{\"name\":\"Bern\"},\"stationboard\":["
               "{\"name\":\"IC 1\",\"category\":\"IC\",\"number\":\"1\","
               "\"to\":\"Zurich HB\",\"passList\":[";
    String padding;
    padding.reserve(220);
    for (int i = 0; i < 220; i++) padding += 'x';
    for (int i = 0; i < 150; i++) {
        if (i > 0) s += ",";
        s += "{\"station\":{\"name\":\"PaddingStationNumber";
        s += String(i);
        s += padding;
        s += "\"},\"departure\":\"2026-09-07T12:34:00+0200\"}";
    }
    s += "],\"stop\":{\"departure\":\"2026-09-07T12:34:00+0200\",\"delay\":null}}]}";
    return s;
}

void test_sb_over_32kib_ignored_passlist_requires_new_stream_cap() {
    String json = buildOver32KiBPassListJson();
    TEST_ASSERT_TRUE_MESSAGE(json.length() > 32768, "fixture must exceed the legacy stream cap");
    TEST_ASSERT_TRUE_MESSAGE(json.length() < 65536, "fixture must fit the new stream cap");

    StationboardSnapshot legacySnapshot;
    StringStream legacyInner(json);
    RequestLimits legacyLimits{32768, HTTP_TIMEOUT, HTTP_TOTAL_TIMEOUT};
    BoundedStream legacyBounded(
        legacyInner, legacyLimits, millis(),
        [](Stream& source) { return static_cast<StringStream&>(source).eof(); });
    TEST_ASSERT_FALSE_MESSAGE(parseStationboard(legacyBounded, legacySnapshot),
                              "legacy 32KiB stream cap must reject the valid truncated body");
    TEST_ASSERT_TRUE_MESSAGE(legacyBounded.limitReached(), "legacy stream must reach its cap");

    StationboardSnapshot snapshot;
    StringStream inner(json);
    BoundedStream bounded(
        inner, defaultLimits(), millis(),
        [](Stream& source) { return static_cast<StringStream&>(source).eof(); });
    bool parsed = parseStationboard(bounded, snapshot);
    TEST_ASSERT_FALSE_MESSAGE(bounded.limitReached(), "new stream cap must not truncate the body");
    TEST_ASSERT_FALSE_MESSAGE(bounded.expired(), "new stream cap must not time out while parsing the body");
    TEST_ASSERT_TRUE_MESSAGE(parsed, "configured stream cap must parse the valid body");
    TEST_ASSERT_EQUAL_STRING("Bern", snapshot.station.c_str());
    TEST_ASSERT_EQUAL_UINT(1, snapshot.count);
    TEST_ASSERT_EQUAL_STRING("Zurich HB", snapshot.rows[0].destination.c_str());
}

// --- Task 6: bounded connections snapshot parser ---
//
// parseConnections() is header-inline in connections.h (Task 5 URLEncode
// precedent) because pio test links only this TU — src/*.cpp is NOT
// linked — so tests exercise the REAL firmware code path.
// Contract: true on success (output fully assigned, receivedAt = millis());
// false on JSON error/overflow with output left UNCHANGED. Walking-only
// entries (empty/missing products) are SKIPPED, not fatal; a doc of only
// such entries parses to count 0 success.

static bool parseConnFromString(const String& json, ConnectionsSnapshot& snap) {
    StringStream stream(json);
    return parseConnections(stream, snap);
}

static const char* CONN_VALID_JSON =
    "{\"connections\":["
    "{\"from\":{\"departure\":\"2026-09-07T08:14:00+0200\",\"departureTimestamp\":1786073640,\"delay\":2},"
    "\"to\":{\"arrival\":\"2026-09-07T09:02:00+0200\"},"
    "\"duration\":\"00d00:48:00\",\"transfers\":1,\"products\":[\"IC 1\"]},"
    "{\"from\":{\"departure\":\"2026-09-07T08:30:00+0200\",\"departureTimestamp\":1786074600,\"delay\":null},"
    "\"to\":{\"arrival\":\"2026-09-07T09:38:00+0200\"},"
    "\"duration\":\"00d01:08:00\",\"transfers\":0,\"products\":[\"S 2\"]}"
    "]}";

void test_conn_valid_doc_parses() {
    ConnectionsSnapshot snap;
    unsigned long before = millis();
    TEST_ASSERT_TRUE_MESSAGE(parseConnFromString(CONN_VALID_JSON, snap), "valid doc must parse");
    TEST_ASSERT_EQUAL_UINT(2, snap.count);
    TEST_ASSERT_EQUAL_STRING("08:14", snap.rows[0].departure.c_str());
    TEST_ASSERT_EQUAL_STRING("09:02", snap.rows[0].arrival.c_str());
    TEST_ASSERT_EQUAL_STRING("48m", snap.rows[0].duration.c_str());
    TEST_ASSERT_EQUAL_STRING("IC 1", snap.rows[0].product.c_str());
    TEST_ASSERT_EQUAL_INT(1, snap.rows[0].transfers);
    TEST_ASSERT_EQUAL_INT(2, snap.rows[0].delay);
    TEST_ASSERT_EQUAL_INT32(1786073640, snap.rows[0].departureTimestamp);
    TEST_ASSERT_EQUAL_STRING("1h8m", snap.rows[1].duration.c_str());
    TEST_ASSERT_EQUAL_INT(0, snap.rows[1].delay);
    TEST_ASSERT_EQUAL_INT32(1786074600, snap.rows[1].departureTimestamp);
    TEST_ASSERT_TRUE_MESSAGE(snap.receivedAt >= before, "receivedAt must be stamped on success");
}

void test_conn_parses_departure_timestamp_after_2038() {
    const char* json =
        "{\"connections\":["
        "{\"from\":{\"departure\":\"2040-01-01T08:14:00+0200\",\"departureTimestamp\":2200000000,\"delay\":0},"
        "\"to\":{\"arrival\":\"2040-01-01T09:02:00+0200\"},"
        "\"duration\":\"00d00:48:00\",\"transfers\":0,\"products\":[\"IC 1\"]}"
        "]}";
    ConnectionsSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseConnFromString(json, snap), "post-2038 document must parse");
    TEST_ASSERT_EQUAL_INT64_MESSAGE(INT64_C(2200000000), snap.rows[0].departureTimestamp,
                                    "departure timestamp must retain all 64 bits");
}

static const char* CONN_REORDERED_JSON =
    "{\"connections\":["
    "{\"products\":[\"RE 5\"],\"transfers\":2,\"duration\":\"00d00:48:00\","
    "\"to\":{\"arrival\":\"2026-09-07T09:02:00+0200\"},"
    "\"from\":{\"delay\":0,\"departureTimestamp\":1786073640,\"departure\":\"2026-09-07T08:14:00+0200\"}}"
    "]}";

void test_conn_reordered_members_parse() {
    ConnectionsSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseConnFromString(CONN_REORDERED_JSON, snap), "reordered doc must parse");
    TEST_ASSERT_EQUAL_UINT(1, snap.count);
    TEST_ASSERT_EQUAL_STRING("08:14", snap.rows[0].departure.c_str());
    TEST_ASSERT_EQUAL_STRING("RE 5", snap.rows[0].product.c_str());
    TEST_ASSERT_EQUAL_INT(2, snap.rows[0].transfers);
}

void test_conn_truncated_fails_and_preserves_output() {
    String truncated = String(CONN_VALID_JSON).substring(0, 120); // cut mid-document
    ConnectionsSnapshot snap;
    snap.count = 1;
    snap.receivedAt = 12345;
    snap.rows[0].departure = "KEEP";
    snap.rows[0].product = "KEEP-P";
    TEST_ASSERT_FALSE_MESSAGE(parseConnFromString(truncated, snap), "truncated doc must fail");
    TEST_ASSERT_EQUAL_UINT(1, snap.count);
    TEST_ASSERT_EQUAL_UINT32(12345, snap.receivedAt);
    TEST_ASSERT_EQUAL_STRING("KEEP", snap.rows[0].departure.c_str());
    TEST_ASSERT_EQUAL_STRING("KEEP-P", snap.rows[0].product.c_str());
}

static const char* CONN_EMPTY_PRODUCTS_JSON =
    "{\"connections\":["
    "{\"from\":{\"departure\":\"2026-09-07T08:14:00+0200\",\"departureTimestamp\":1786073640,\"delay\":0},"
    "\"to\":{\"arrival\":\"2026-09-07T09:02:00+0200\"},"
    "\"duration\":\"00d00:48:00\",\"transfers\":1,\"products\":[\"IC 1\"]},"
    "{\"from\":{\"departure\":\"2026-09-07T08:15:00+0200\",\"departureTimestamp\":1786073700,\"delay\":0},"
    "\"to\":{\"arrival\":\"2026-09-07T08:45:00+0200\"},"
    "\"duration\":\"00d00:30:00\",\"transfers\":0,\"products\":[]},"
    "{\"from\":{\"departure\":\"2026-09-07T08:30:00+0200\",\"departureTimestamp\":1786074600,\"delay\":0},"
    "\"to\":{\"arrival\":\"2026-09-07T09:38:00+0200\"},"
    "\"duration\":\"00d01:08:00\",\"transfers\":0,\"products\":[\"S 2\"]}"
    "]}";

void test_conn_empty_products_skipped() {
    ConnectionsSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseConnFromString(CONN_EMPTY_PRODUCTS_JSON, snap), "doc with walking entry must parse");
    TEST_ASSERT_EQUAL_UINT(2, snap.count);
    TEST_ASSERT_EQUAL_STRING("IC 1", snap.rows[0].product.c_str());
    TEST_ASSERT_EQUAL_STRING("S 2", snap.rows[1].product.c_str());
}

static const char* CONN_NESTED_JSON =
    "{\"connections\":["
    "{\"from\":{\"departure\":\"2026-09-07T08:14:00+0200\",\"departureTimestamp\":1786073640,\"delay\":0,"
    "\"station\":{\"name\":\"FAKE\"},\"location\":{\"name\":\"FAKE\"}},"
    "\"to\":{\"arrival\":\"2026-09-07T09:02:00+0200\",\"station\":{\"name\":\"FAKE\"}},"
    "\"duration\":\"00d00:48:00\",\"transfers\":1,\"products\":[\"IC 1\"],"
    "\"sections\":[{\"journey\":{\"name\":\"FAKE\",\"passList\":[{\"station\":{\"name\":\"FAKE\"}}]}}]"
    "}]}";

void test_conn_nested_journeys_ignored() {
    ConnectionsSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseConnFromString(CONN_NESTED_JSON, snap), "nested doc must parse");
    TEST_ASSERT_EQUAL_UINT(1, snap.count);
    TEST_ASSERT_EQUAL_STRING("08:14", snap.rows[0].departure.c_str());
    TEST_ASSERT_EQUAL_STRING("09:02", snap.rows[0].arrival.c_str());
    TEST_ASSERT_EQUAL_STRING("IC 1", snap.rows[0].product.c_str());
}

static String buildManyConnectionsJson(int n) {
    String s = "{\"connections\":[";
    for (int i = 0; i < n; i++) {
        if (i > 0) s += ",";
        s += "{\"from\":{\"departure\":\"2026-09-07T08:14:00+0200\",\"departureTimestamp\":1786073640,\"delay\":0},";
        s += "\"to\":{\"arrival\":\"2026-09-07T09:02:00+0200\"},";
        s += "\"duration\":\"00d00:48:00\",\"transfers\":0,\"products\":[\"S ";
        s += String(i);
        s += "\"]}";
    }
    s += "]}";
    return s;
}

void test_conn_more_than_eight_capped() {
    ConnectionsSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseConnFromString(buildManyConnectionsJson(10), snap), "10-entry doc must parse");
    TEST_ASSERT_EQUAL_UINT(MAX_CONNECTIONS, snap.count);
    TEST_ASSERT_EQUAL_STRING("S 0", snap.rows[0].product.c_str());
    TEST_ASSERT_EQUAL_STRING("S 7", snap.rows[7].product.c_str());
}

static const char* CONN_MULTIDAY_JSON =
    "{\"connections\":["
    "{\"from\":{\"departure\":\"2026-09-07T08:14:00+0200\",\"departureTimestamp\":1786073640,\"delay\":0},"
    "\"to\":{\"arrival\":\"2026-09-08T10:44:00+0200\"},"
    "\"duration\":\"01d02:30:00\",\"transfers\":2,\"products\":[\"EC 1\"]}"
    "]}";

void test_conn_multiday_duration() {
    ConnectionsSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseConnFromString(CONN_MULTIDAY_JSON, snap), "multi-day doc must parse");
    TEST_ASSERT_EQUAL_UINT(1, snap.count);
    TEST_ASSERT_EQUAL_STRING("26h30m", snap.rows[0].duration.c_str());
}

static const char* CONN_WALKING_ONLY_JSON =
    "{\"connections\":["
    "{\"from\":{\"departure\":\"2026-09-07T08:15:00+0200\",\"departureTimestamp\":1786073700,\"delay\":0},"
    "\"to\":{\"arrival\":\"2026-09-07T08:45:00+0200\"},"
    "\"duration\":\"00d00:30:00\",\"transfers\":0,\"products\":[]}"
    "]}";

void test_conn_walking_only_returns_empty_success() {
    ConnectionsSnapshot snap;
    TEST_ASSERT_TRUE_MESSAGE(parseConnFromString(CONN_WALKING_ONLY_JSON, snap), "walking-only doc must succeed");
    TEST_ASSERT_EQUAL_UINT(0, snap.count);
}

// --- Task 7: bounded HTTP transactions + truthful BTC status ---
//
// http_request.h is header-inline (Tasks 5/6 precedent) because pio test
// links only this TU — src/*.cpp is NOT linked — so tests exercise the
// REAL firmware code path.

void test_request_limits_defaults() {
    RequestLimits limits = defaultLimits();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(MAX_API_RESPONSE_BYTES, limits.maxBytes,
                                     "byte cap must equal MAX_API_RESPONSE_BYTES");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(HTTP_TIMEOUT, limits.inactivityMs,
                                     "inactivity must equal HTTP_TIMEOUT");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(HTTP_TOTAL_TIMEOUT, limits.totalMs,
                                     "total must equal HTTP_TOTAL_TIMEOUT");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(30000, HTTP_TOTAL_TIMEOUT,
                                     "total timeout must be 30000ms");
}

void test_bounded_stream_stops_at_max_bytes() {
    String payload;
    for (int i = 0; i < 100; i++) payload += (char)('A' + (i % 26));
    StringStream inner(payload);
    RequestLimits limits{10, HTTP_TIMEOUT, HTTP_TOTAL_TIMEOUT};
    BoundedStream bounded(inner, limits);
    int count = 0;
    int firstTen[10];
    while (true) {
        int c = bounded.read();
        if (c < 0) break;
        if (count < 10) firstTen[count] = c;
        count++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(10, count, "100-byte feed with limit 10 must yield 10 bytes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, bounded.read(), "reads past the cap must return -1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bounded.available(), "available() must be 0 past the cap");
    TEST_ASSERT_TRUE_MESSAGE(bounded.limitReached(), "cap flag must be set");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(10, bounded.bytesRead(), "byte counter must stop at 10");
    TEST_ASSERT_EQUAL_INT_MESSAGE('A', firstTen[0], "stream content must pass through unaltered");
    TEST_ASSERT_EQUAL_INT_MESSAGE('J', firstTen[9], "stream content must pass through unaltered");
}

void test_bounded_stream_readbytes_stops_at_max_bytes() {
    StringStream inner("abcdefghijklmnopqrstuvwxyz");
    RequestLimits limits{10, HTTP_TIMEOUT, HTTP_TOTAL_TIMEOUT};
    BoundedStream bounded(inner, limits);
    char received[16] = {};

    size_t count = bounded.readBytes(received, sizeof(received));

    TEST_ASSERT_EQUAL_UINT_MESSAGE(10, count, "readBytes must not exceed the byte cap");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("abcdefghij", received, 10,
                                         "readBytes must preserve the bounded prefix");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bounded.available(), "available() must be 0 past the cap");
}

namespace {
unsigned long boundedTestNow = 0;

unsigned long boundedTestClock() {
    return boundedTestNow;
}

void advanceBoundedTestClock() {
    boundedTestNow++;
}

class GappedStream : public Stream {
public:
    GappedStream(const String& input, int gapsBeforeData)
        : data(input), gaps(gapsBeforeData), pos(0) {}

    int available() override {
        return gaps > 0 ? 0 : (int)data.length() - (int)pos;
    }

    int read() override {
        if (gaps > 0) {
            gaps--;
            return -1;
        }
        if (pos >= data.length()) return -1;
        return (unsigned char)data[pos++];
    }

    int peek() override {
        if (gaps > 0 || pos >= data.length()) return -1;
        return (unsigned char)data[pos];
    }

    size_t write(uint8_t) override { return 0; }
    bool eof() const { return gaps == 0 && pos >= data.length(); }

private:
    String data;
    int gaps;
    size_t pos;
};

bool gappedStreamEof(Stream& stream) {
    return static_cast<GappedStream&>(stream).eof();
}

bool stringStreamEof(Stream& stream) {
    return static_cast<StringStream&>(stream).eof();
}
} // namespace

void test_bounded_stream_readbytes_waits_through_packet_gap() {
    boundedTestNow = 0;
    GappedStream inner("hello", 2);
    RequestLimits limits{10, 5, 20};
    BoundedStream bounded(inner, limits, 0, gappedStreamEof,
                          boundedTestClock, advanceBoundedTestClock);
    char received[6] = {};

    size_t count = bounded.readBytes(received, 5);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(5, count, "packet gaps must not truncate bulk reads");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("hello", received, "bytes after the gap must be read");
    TEST_ASSERT_FALSE_MESSAGE(bounded.expired(), "short gaps must remain within inactivity budget");
}

void test_bounded_stream_readbytes_expires_after_inactivity() {
    boundedTestNow = 0;
    GappedStream inner("", 100);
    RequestLimits limits{10, 3, 20};
    BoundedStream bounded(inner, limits, 0, gappedStreamEof,
                          boundedTestClock, advanceBoundedTestClock);
    char received[1] = {};

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, bounded.readBytes(received, sizeof(received)),
                                   "no data must be returned after the inactivity deadline");
    TEST_ASSERT_TRUE_MESSAGE(bounded.expired(), "wrapper must enforce inactivity while polling");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, boundedTestNow, "polling must stop exactly at the deadline");
}

void test_bounded_stream_readbytes_expires_after_total_timeout() {
    boundedTestNow = 0;
    GappedStream inner("", 100);
    RequestLimits limits{10, 20, 3};
    BoundedStream bounded(inner, limits, 0, gappedStreamEof,
                          boundedTestClock, advanceBoundedTestClock);
    char received[1] = {};

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, bounded.readBytes(received, sizeof(received)),
                                   "no data must be returned after the total deadline");
    TEST_ASSERT_TRUE_MESSAGE(bounded.expired(), "wrapper must enforce total timeout while polling");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, boundedTestNow, "polling must stop at the total deadline");
}

void test_bounded_stream_rejects_eof_at_deadline() {
    boundedTestNow = 3;
    RequestLimits limits{10, 20, 3};
    StringStream readInner("");
    BoundedStream readBounded(readInner, limits, 0, stringStreamEof,
                              boundedTestClock, advanceBoundedTestClock);

    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, readBounded.read(), "EOF at the deadline must not be accepted");
    TEST_ASSERT_TRUE_MESSAGE(readBounded.expired(), "deadline must take precedence over EOF");
    TEST_ASSERT_FALSE_MESSAGE(readBounded.eofReached(), "deadline must not mark EOF reached");
    TEST_ASSERT_FALSE_MESSAGE(consumeToEnd(readBounded), "draining at the deadline must fail");

    StringStream peekInner("");
    BoundedStream peekBounded(peekInner, limits, 0, stringStreamEof,
                              boundedTestClock, advanceBoundedTestClock);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, peekBounded.peek(), "peek must reject EOF at the deadline");
    TEST_ASSERT_TRUE_MESSAGE(peekBounded.expired(), "peek must preserve the deadline failure");
    TEST_ASSERT_FALSE_MESSAGE(peekBounded.eofReached(), "peek must not mark EOF at the deadline");
}

void test_content_length_policy_rejects_known_oversize_body() {
    RequestLimits limits{10, HTTP_TIMEOUT, HTTP_TOTAL_TIMEOUT};
    TEST_ASSERT_TRUE_MESSAGE(isContentLengthAllowed(-1, limits), "unknown lengths are stream-capped");
    TEST_ASSERT_TRUE_MESSAGE(isContentLengthAllowed(10, limits), "the exact cap is allowed");
    TEST_ASSERT_FALSE_MESSAGE(isContentLengthAllowed(11, limits), "known oversized body must be rejected");
}

void test_consume_to_end_rejects_tail_that_reaches_cap() {
    boundedTestNow = 0;
    StringStream inner("01234567890");
    RequestLimits limits{10, 5, 20};
    BoundedStream bounded(inner, limits, 0, stringStreamEof,
                          boundedTestClock, advanceBoundedTestClock);

    TEST_ASSERT_FALSE_MESSAGE(consumeToEnd(bounded),
                              "a valid JSON prefix with an oversized tail must fail the drain");
    TEST_ASSERT_TRUE_MESSAGE(bounded.limitReached(), "drain must report the byte cap");
    TEST_ASSERT_TRUE_MESSAGE(inner.eof(), "the overflow byte must be observed, not returned");
}

void test_consume_to_end_accepts_body_exactly_at_cap() {
    boundedTestNow = 0;
    StringStream inner("0123456789");
    RequestLimits limits{10, 5, 20};
    BoundedStream bounded(inner, limits, 0, stringStreamEof,
                          boundedTestClock, advanceBoundedTestClock);

    TEST_ASSERT_TRUE_MESSAGE(consumeToEnd(bounded),
                             "a body exactly at the cap followed by EOF must succeed");
    TEST_ASSERT_FALSE_MESSAGE(bounded.limitReached(), "clean EOF must not report an overflow");
}

void test_consume_to_end_zero_cap_accepts_only_empty_body() {
    boundedTestNow = 0;
    RequestLimits limits{0, 5, 20};
    StringStream empty("");
    StringStream nonEmpty("x");
    BoundedStream emptyBounded(empty, limits, 0, stringStreamEof,
                               boundedTestClock, advanceBoundedTestClock);
    BoundedStream nonEmptyBounded(nonEmpty, limits, 0, stringStreamEof,
                                  boundedTestClock, advanceBoundedTestClock);

    TEST_ASSERT_TRUE_MESSAGE(consumeToEnd(emptyBounded), "zero cap must accept an empty body");
    TEST_ASSERT_FALSE_MESSAGE(emptyBounded.limitReached(), "empty body must not overflow zero cap");
    TEST_ASSERT_FALSE_MESSAGE(consumeToEnd(nonEmptyBounded), "zero cap must reject any body byte");
    TEST_ASSERT_TRUE_MESSAGE(nonEmptyBounded.limitReached(), "first byte must overflow zero cap");
}

void test_is_expired_basic() {
    TEST_ASSERT_TRUE_MESSAGE(isExpired(1000, 1500, 400), "500ms elapsed must exceed 400ms limit");
    TEST_ASSERT_FALSE_MESSAGE(isExpired(1000, 1200, 400), "200ms elapsed must not exceed 400ms limit");
    TEST_ASSERT_TRUE_MESSAGE(isExpired(1000, 1400, 400), "exactly-at-limit must count as expired");
    TEST_ASSERT_FALSE_MESSAGE(isExpired(1000, 1000, 400), "zero elapsed must not expire");
}

void test_is_expired_wraparound() {
    // millis() rolls over every ~49.7 days; unsigned subtraction keeps
    // the math correct across the wrap (start near 2^32, now just past 0).
    const unsigned long start = 0xFFFFFFF0UL; // 2^32 - 16
    const unsigned long now = 0x00000005UL;   // 21 ticks later
    TEST_ASSERT_TRUE_MESSAGE(isExpired(start, now, 20), "21 ticks elapsed must exceed 20 limit");
    TEST_ASSERT_FALSE_MESSAGE(isExpired(start, now, 30), "21 ticks elapsed must not exceed 30 limit");
    TEST_ASSERT_TRUE_MESSAGE(isExpired(start, now, 21), "exactly-at-limit across wrap must expire");
}

void test_btc_verdict() {
    TEST_ASSERT_TRUE_MESSAGE(btcVerdict(200, true) == FetchResult::Success,
                             "200 + valid payload must succeed");
    TEST_ASSERT_TRUE_MESSAGE(btcVerdict(200, false) == FetchResult::ParseError,
                             "200 + invalid JSON must be a parse failure");
    TEST_ASSERT_TRUE_MESSAGE(btcVerdict(500, true) == FetchResult::HttpError,
                             "non-200 must be an HTTP failure");
    TEST_ASSERT_TRUE_MESSAGE(btcVerdict(500, false) == FetchResult::HttpError,
                             "non-200 with bad payload must be an HTTP failure");
    TEST_ASSERT_TRUE_MESSAGE(btcVerdict(-1, false) == FetchResult::HttpError,
                             "connection failure must be an HTTP failure");
}

void test_btc_price_parse_valid() {
    String out;
    TEST_ASSERT_TRUE_MESSAGE(
        parseBtcPrice("{\"data\":{\"base\":\"BTC\",\"currency\":\"USD\",\"amount\":\"97123.45\"}}", out),
        "coinbase spot payload must parse");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("97123", out.c_str(), "price must truncate to whole dollars");
}

void test_btc_price_parse_invalid() {
    String out = "KEEP";
    TEST_ASSERT_FALSE_MESSAGE(parseBtcPrice("{not json", out), "malformed JSON must fail");
    TEST_ASSERT_FALSE_MESSAGE(parseBtcPrice("{\"data\":{}}", out), "missing amount must fail");
    TEST_ASSERT_FALSE_MESSAGE(parseBtcPrice("{\"error\":\"rate limit\"}", out), "error doc must fail");
    TEST_ASSERT_FALSE_MESSAGE(parseBtcPrice("{\"data\":{\"amount\":\"not-a-price\"}}", out),
                              "non-numeric amount must fail");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("KEEP", out.c_str(), "output must be untouched on failure");
}

// --- Task 8: snapshot freshness and aggregate refresh status ---

void test_stationboard_snapshot_freshness_handles_millis_rollover() {
    const unsigned long receivedAt = 0xFFFFFFF0UL;
    const unsigned long now = 0x00000005UL;
    TEST_ASSERT_TRUE_MESSAGE(isSnapshotFresh(receivedAt, now, 22),
                             "21ms across rollover must remain fresh before the age limit");
    TEST_ASSERT_FALSE_MESSAGE(isSnapshotFresh(receivedAt, now, 21),
                              "the age limit itself must mark a snapshot stale");
}

void test_connections_snapshot_expires_at_first_effective_departure() {
    ConnectionsSnapshot snapshot;
    snapshot.count = 1;
    snapshot.rows[0].departureTimestamp = 1000;
    snapshot.rows[0].delay = 2;
    TEST_ASSERT_FALSE_MESSAGE(connectionsSnapshotExpired(snapshot, 1119),
                              "connection must remain shown before delayed departure");
    TEST_ASSERT_TRUE_MESSAGE(connectionsSnapshotExpired(snapshot, 1120),
                             "connection must clear at delayed departure");
}

void test_connections_snapshot_expiry_saturates_64_bit_deadline() {
    ConnectionsSnapshot snapshot;
    snapshot.count = 1;
    snapshot.rows[0].departureTimestamp = INT64_MAX - 30;
    snapshot.rows[0].delay = 1;
    TEST_ASSERT_FALSE_MESSAGE(connectionsSnapshotExpired(snapshot, INT64_MAX - 1),
                              "an overflowing delay must not wrap the departure deadline");
    TEST_ASSERT_TRUE_MESSAGE(connectionsSnapshotExpired(snapshot, INT64_MAX),
                             "a saturated departure deadline expires at the largest epoch");
}

void test_empty_connections_snapshot_is_not_expired() {
    ConnectionsSnapshot snapshot;
    TEST_ASSERT_FALSE_MESSAGE(connectionsSnapshotExpired(snapshot, 999999),
                              "a valid empty result remains renderable until the next fetch");
}

void test_transport_status_ignores_optional_btc_result() {
    RefreshResult refresh{FetchResult::ParseError, FetchResult::Success, 0};
    TEST_ASSERT_FALSE_MESSAGE(isTransportFreshResult(refresh.transport),
                              "BTC success must not turn a failed transport refresh green");
}

void test_partial_transport_result_is_not_fresh() {
    TEST_ASSERT_FALSE_MESSAGE(isTransportFreshResult(FetchResult::ParseError),
                              "a parse error is not a complete transport refresh");
}

// --- Task 11: recoverable OTA state machine ---

void test_ota_starts_only_with_credentials_and_without_portal() {
    TEST_ASSERT_FALSE_MESSAGE(otaCanStart(false, false), "OTA requires configured credentials");
    TEST_ASSERT_FALSE_MESSAGE(otaCanStart(true, true), "portal owns port 80");
    TEST_ASSERT_TRUE_MESSAGE(otaCanStart(false, true), "credentialed OTA may own port 80");
}

void test_ota_activation_awaits_upload() {
    OtaStateMachine state;
    otaActivate(state, 100);
    TEST_ASSERT_EQUAL(static_cast<int>(OtaState::AwaitingUpload), static_cast<int>(state.current));
    TEST_ASSERT_EQUAL_UINT32(100, state.activatedAt);
    TEST_ASSERT_EQUAL_UINT32(100, state.lastProgressAt);
}

void test_ota_uploading_records_progress() {
    OtaStateMachine state;
    otaActivate(state, 100);
    otaUploadStarted(state, 120);
    otaUploadProgressed(state, 150);
    TEST_ASSERT_EQUAL(static_cast<int>(OtaState::Uploading), static_cast<int>(state.current));
    TEST_ASSERT_EQUAL_UINT32(150, state.lastProgressAt);
}

void test_ota_upload_completion_records_success_and_failure() {
    OtaStateMachine state;
    otaActivate(state, 100);
    otaUploadFinished(state, true);
    TEST_ASSERT_EQUAL(static_cast<int>(OtaState::Succeeded), static_cast<int>(state.current));
    otaActivate(state, 200);
    otaUploadFinished(state, false);
    TEST_ASSERT_EQUAL(static_cast<int>(OtaState::Failed), static_cast<int>(state.current));
}

void test_ota_awaiting_upload_times_out() {
    OtaStateMachine state;
    otaActivate(state, 100);
    TEST_ASSERT_TRUE_MESSAGE(otaMustStop(state, 100 + OTA_AWAIT_UPLOAD_TIMEOUT_MS, true),
                             "an idle OTA page must expire");
}

void test_ota_upload_stalls_time_out_from_last_progress() {
    OtaStateMachine state;
    otaActivate(state, 100);
    otaUploadStarted(state, 110);
    otaUploadProgressed(state, 120);
    TEST_ASSERT_FALSE_MESSAGE(otaMustStop(state, 120 + OTA_UPLOAD_INACTIVITY_TIMEOUT_MS - 1, true),
                              "recent upload progress remains active");
    TEST_ASSERT_TRUE_MESSAGE(otaMustStop(state, 120 + OTA_UPLOAD_INACTIVITY_TIMEOUT_MS, true),
                             "stalled upload must recover");
}

void test_ota_wifi_loss_forces_recovery() {
    OtaStateMachine state;
    otaActivate(state, 100);
    TEST_ASSERT_TRUE_MESSAGE(otaMustStop(state, 101, false), "WiFi loss must release OTA server");
}

void test_ota_upload_survives_night_mode_boundary() {
    OtaStateMachine state;
    otaActivate(state, 100);
    otaUploadStarted(state, 110);
    TEST_ASSERT_FALSE_MESSAGE(otaMustStop(state, 111, true),
                              "night mode must not stop an in-progress upload");
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_operational_limits_are_bounded);
    RUN_TEST(test_url_encode_handles_utf8_bytes);
    RUN_TEST(test_stationboard_url_requests_only_parsed_fields);
    RUN_TEST(test_config_rejects_empty_stations);
    RUN_TEST(test_config_clamps_numeric_ranges);
    RUN_TEST(test_equal_night_times_disable_schedule);
    RUN_TEST(test_reconnect_state_is_due_initially);
    RUN_TEST(test_reconnect_failures_use_capped_exponential_backoff);
    RUN_TEST(test_reconnect_due_handles_millis_rollover);
    RUN_TEST(test_reconnect_due_handles_deadline_wrapping_to_zero);
    RUN_TEST(test_reconnect_success_resets_scheduler);
    RUN_TEST(test_refresh_attempt_interval_handles_failed_attempts_and_rollover);
    RUN_TEST(test_plausible_epoch_requires_2024_or_later);
    RUN_TEST(test_failed_clock_sync_keeps_an_invalid_clock_invalid);
    RUN_TEST(test_epoch_timeval_conversion_has_zero_microseconds);
    RUN_TEST(test_invalid_clock_disables_night_mode);
    RUN_TEST(test_night_schedule_handles_overnight_boundaries);
    RUN_TEST(test_night_schedule_uses_local_time_across_dst_boundaries);
    RUN_TEST(test_weekend_disablement_overrides_an_active_night_schedule);
    RUN_TEST(test_clock_retry_interval_handles_millis_rollover);
    RUN_TEST(test_temporary_wake_expiry_handles_millis_rollover);
    RUN_TEST(test_wake_policy_ignores_wifi_and_unknown_wakes);
    RUN_TEST(test_portal_parameters_have_program_lifetime);
    RUN_TEST(test_sb_valid_doc_parses);
    RUN_TEST(test_sb_reordered_members_parse);
    RUN_TEST(test_sb_nested_unrelated_keys_ignored);
    RUN_TEST(test_sb_truncated_fails_and_preserves_output);
    RUN_TEST(test_sb_more_than_ten_entries_capped);
    RUN_TEST(test_sb_unicode_escapes_decoded);
    RUN_TEST(test_sb_large_ignored_passlist_succeeds);
    RUN_TEST(test_sb_over_32kib_ignored_passlist_requires_new_stream_cap);
    RUN_TEST(test_conn_valid_doc_parses);
    RUN_TEST(test_conn_parses_departure_timestamp_after_2038);
    RUN_TEST(test_conn_reordered_members_parse);
    RUN_TEST(test_conn_truncated_fails_and_preserves_output);
    RUN_TEST(test_conn_empty_products_skipped);
    RUN_TEST(test_conn_nested_journeys_ignored);
    RUN_TEST(test_conn_more_than_eight_capped);
    RUN_TEST(test_conn_multiday_duration);
    RUN_TEST(test_conn_walking_only_returns_empty_success);
    RUN_TEST(test_request_limits_defaults);
    RUN_TEST(test_bounded_stream_stops_at_max_bytes);
    RUN_TEST(test_bounded_stream_readbytes_stops_at_max_bytes);
    RUN_TEST(test_bounded_stream_readbytes_waits_through_packet_gap);
    RUN_TEST(test_bounded_stream_readbytes_expires_after_inactivity);
    RUN_TEST(test_bounded_stream_readbytes_expires_after_total_timeout);
    RUN_TEST(test_bounded_stream_rejects_eof_at_deadline);
    RUN_TEST(test_content_length_policy_rejects_known_oversize_body);
    RUN_TEST(test_consume_to_end_rejects_tail_that_reaches_cap);
    RUN_TEST(test_consume_to_end_accepts_body_exactly_at_cap);
    RUN_TEST(test_consume_to_end_zero_cap_accepts_only_empty_body);
    RUN_TEST(test_is_expired_basic);
    RUN_TEST(test_is_expired_wraparound);
    RUN_TEST(test_btc_verdict);
    RUN_TEST(test_btc_price_parse_valid);
    RUN_TEST(test_btc_price_parse_invalid);
    RUN_TEST(test_stationboard_snapshot_freshness_handles_millis_rollover);
    RUN_TEST(test_connections_snapshot_expires_at_first_effective_departure);
    RUN_TEST(test_connections_snapshot_expiry_saturates_64_bit_deadline);
    RUN_TEST(test_empty_connections_snapshot_is_not_expired);
    RUN_TEST(test_transport_status_ignores_optional_btc_result);
    RUN_TEST(test_partial_transport_result_is_not_fresh);
    RUN_TEST(test_ota_starts_only_with_credentials_and_without_portal);
    RUN_TEST(test_ota_activation_awaits_upload);
    RUN_TEST(test_ota_uploading_records_progress);
    RUN_TEST(test_ota_upload_completion_records_success_and_failure);
    RUN_TEST(test_ota_awaiting_upload_times_out);
    RUN_TEST(test_ota_upload_stalls_time_out_from_last_progress);
    RUN_TEST(test_ota_wifi_loss_forces_recovery);
    RUN_TEST(test_ota_upload_survives_night_mode_boundary);
    bool spiffsReady = SPIFFS.begin(false);
    if (!spiffsReady) { spiffsReady = SPIFFS.begin(true); }
    if (!spiffsReady) {
        TEST_FAIL_MESSAGE("SPIFFS mount failed - cannot run persistence tests");
    } else {
        RUN_TEST(test_valid_primary_load_keeps_values);
        RUN_TEST(test_corrupt_primary_valid_backup_recovers_backup_values);
        RUN_TEST(test_incomplete_temp_file_is_rejected);
        RUN_TEST(test_save_returns_true_on_success);
        RUN_TEST(test_double_save_rotates_backup);
    }
    UNITY_END();
}

void loop() {}
