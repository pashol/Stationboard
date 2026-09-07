#include <Arduino.h>
#include <unity.h>
#include <SPIFFS.h>
#include <FS.h>
#include "globals.h"
#include "utilities.h"
#include "networking.h"

// Test-local definition: pio test links only this TU + libs (src/*.cpp is
// not linked), so the `config` global referenced by the header-inline
// persistence core is provided here. Firmware links globals.cpp instead.
Config config;

// Test-local portal owner for the same reason: the firmware's `wm` from
// globals.cpp is unavailable here. This instance lets the lifetime probe
// exercise the identical getPortalParameter() lookup.
WiFiManager wm;

void test_operational_limits_are_bounded() {
    TEST_ASSERT_EQUAL_UINT32(10, MAX_TRANSPORTS);
    TEST_ASSERT_EQUAL_UINT32(8, MAX_CONNECTIONS);
    TEST_ASSERT_EQUAL_UINT32(32768, MAX_API_RESPONSE_BYTES);
    TEST_ASSERT_EQUAL_UINT32(8192, STATIONBOARD_JSON_CAPACITY);
    TEST_ASSERT_EQUAL_UINT32(8192, CONNECTIONS_JSON_CAPACITY);
}

void test_url_encode_handles_utf8_bytes() {
    TEST_ASSERT_EQUAL_STRING("Z%C3%BCrich%20HB", URLEncode("Zürich HB").c_str());
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

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_operational_limits_are_bounded);
    RUN_TEST(test_url_encode_handles_utf8_bytes);
    RUN_TEST(test_config_rejects_empty_stations);
    RUN_TEST(test_config_clamps_numeric_ranges);
    RUN_TEST(test_equal_night_times_disable_schedule);
    RUN_TEST(test_portal_parameters_have_program_lifetime);
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
