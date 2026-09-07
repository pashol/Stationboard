#include <Arduino.h>
#include <unity.h>
#include "globals.h"
#include "utilities.h"

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

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_operational_limits_are_bounded);
    RUN_TEST(test_url_encode_handles_utf8_bytes);
    RUN_TEST(test_config_rejects_empty_stations);
    RUN_TEST(test_config_clamps_numeric_ranges);
    RUN_TEST(test_equal_night_times_disable_schedule);
    UNITY_END();
}

void loop() {}
