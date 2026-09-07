#include <Arduino.h>
#include <unity.h>
#include "globals.h"

void test_operational_limits_are_bounded() {
    TEST_ASSERT_EQUAL_UINT32(10, MAX_TRANSPORTS);
    TEST_ASSERT_EQUAL_UINT32(8, MAX_CONNECTIONS);
    TEST_ASSERT_EQUAL_UINT32(32768, MAX_API_RESPONSE_BYTES);
    TEST_ASSERT_EQUAL_UINT32(8192, STATIONBOARD_JSON_CAPACITY);
    TEST_ASSERT_EQUAL_UINT32(8192, CONNECTIONS_JSON_CAPACITY);
}

void setUp(void) {}
void tearDown(void) {}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_operational_limits_are_bounded);
    UNITY_END();
}

void loop() {}
