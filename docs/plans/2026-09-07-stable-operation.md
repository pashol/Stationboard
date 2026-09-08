# Stable Operation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the current stationboard, connections, configuration, night-mode, sleep, and OTA behavior safe for unattended operation under malformed data, low memory, power loss, and prolonged network outages.

**Architecture:** Replace the unsafe JSON SAX dependency and whole-response buffering with bounded ArduinoJson filtering into fixed-capacity snapshots. Separate fetching, validation, publication, and rendering so failed work never replaces known-good data. Convert portal, reconnect, stale-data, sleep, and OTA handling into bounded state transitions that always retain a recovery path.

**Tech Stack:** ESP32 Arduino 2.0.14, PlatformIO, ArduinoJson 6.21.5, WiFiManager 2.0.17, TFT_eSPI, ElegantOTA, Unity/PlatformIO device tests.

---

## Operating Invariants

Implementation is complete only when all of these remain true:

- No API response can grow a container or JSON allocation beyond a compile-time limit.
- A failed or partial request never replaces the last complete valid snapshot.
- Stale transport data is visibly distinguishable and expires according to its data type.
- Every network operation has connect, inactivity, total-duration, and response-size limits.
- WiFi loss never creates a tight retry loop or permanent boot-reset cycle.
- Configuration is validated identically whether loaded from flash or submitted through the portal.
- A power loss while saving configuration leaves either the old or new valid file available.
- Portal and OTA parameters outlive every server that references them.
- Portal, OTA, normal refresh, and night mode cannot own port 80 or the display concurrently.
- OTA has authentication, a valid update partition, inactivity recovery, and failure recovery.
- Night mode is not enabled from an unverified clock.
- Display allocation failure preserves the previous visible frame and does not dereference a failed sprite.

## Task 1: Preserve and Measure the Current Baseline

**Files:**
- Modify: `src/utilities.cpp:178-193`
- Create: `test/test_stability/test_main.cpp`
- Modify: `platformio.ini:15-61`

**Step 1: Record the current firmware-only state**

Review `git diff` and stage only the current firmware work and its design document. Exclude `.vscode/`, `.claude/`, and `blog-post.md` unless explicitly requested.

Run:

```powershell
git status --short
git diff -- src platformio.ini docs/plans/2026-09-07-commute-mode-design.md
```

Expected: the connections/night-mode work to preserve is understood before stability changes begin.

**Step 2: Add a failing device smoke test**

Create a Unity test that asserts the fixed capacities and configuration defaults that later tasks will expose:

```cpp
#include <Arduino.h>
#include <unity.h>
#include "globals.h"

void test_operational_limits_are_bounded() {
    TEST_ASSERT_EQUAL_UINT32(10, MAX_TRANSPORTS);
    TEST_ASSERT_EQUAL_UINT32(8, MAX_CONNECTIONS);
    TEST_ASSERT_TRUE(MAX_API_RESPONSE_BYTES <= 32768);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_operational_limits_are_bounded);
    UNITY_END();
}

void loop() {}
```

**Step 3: Build to verify the test fails**

Run:

```powershell
& "$HOME\.platformio\penv\Scripts\pio.exe" test -e ESP32-2432S028R --without-uploading
```

Expected: FAIL to compile because the three limits do not exist.

**Step 4: Add limits and heap phase logging**

Add to `globals.h` and define in `globals.cpp`:

```cpp
constexpr size_t MAX_TRANSPORTS = 10;
constexpr size_t MAX_CONNECTIONS = 8;
constexpr size_t MAX_API_RESPONSE_BYTES = 32768;
constexpr size_t STATIONBOARD_JSON_CAPACITY = 8192;
constexpr size_t CONNECTIONS_JSON_CAPACITY = 8192;
```

Add a diagnostic helper that reports both total and largest allocatable heap block:

```cpp
void logHeap(const char* phase) {
    Serial.printf("Heap[%s]: free=%u largest=%u min=%u\n",
                  phase,
                  ESP.getFreeHeap(),
                  ESP.getMaxAllocHeap(),
                  ESP.getMinFreeHeap());
}
```

Call it before HTTP, after parsing, before sprite allocation, and after rendering while developing. Keep one summary call per refresh after the memory work is stable.

**Step 5: Build and run the device test**

Run the `--without-uploading` command first, then run without that flag on connected hardware.

Expected: build succeeds; device reports one passing test.

**Step 6: Commit**

```powershell
git add src/globals.h src/globals.cpp src/utilities.h src/utilities.cpp test/test_stability/test_main.cpp platformio.ini
git commit -m "test: define stationboard stability limits"
```

## Task 2: Centralize Strict Configuration Validation

**Files:**
- Modify: `src/globals.h:12-26`
- Modify: `src/utilities.h:14-30`
- Modify: `src/utilities.cpp:72-87,145-165,195-267,372-395`
- Modify: `test/test_stability/test_main.cpp`

**Step 1: Write failing tests**

Cover:

```cpp
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
```

**Step 2: Run to verify failure**

Expected: missing validation functions and incorrect UTF-8 encoding.

**Step 3: Implement one validation path**

Add documented constants for maximum station length and offset. Implement `normalizeConfiguration(Config&)` and `validateConfiguration(const Config&)`. Use unsigned bytes in URL encoding:

```cpp
for (unsigned char c : msg) {
    if (isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '~') {
        encodedMsg += static_cast<char>(c);
    } else {
        encodedMsg += '%';
        encodedMsg += hex[(c >> 4) & 0x0F];
        encodedMsg += hex[c & 0x0F];
    }
}
```

Use checked `int64_t` arithmetic before converting the station offset to `time_t`.

**Step 4: Apply validation at every ingress**

Load JSON into a temporary `Config`, normalize and validate it, then assign to global `config`. Portal submission must call the same functions before saving or applying values. `updateBrightness()` must defensively constrain its index before reading `BRIGHTNESS_LEVELS`.

**Step 5: Run tests and build**

Run device tests and `pio run`.

Expected: UTF-8, malformed configuration, equal night times, brightness, limit, offset, hour, and minute cases pass.

**Step 6: Commit**

```powershell
git add src/globals.h src/globals.cpp src/utilities.h src/utilities.cpp test/test_stability/test_main.cpp
git commit -m "fix: validate configuration at every ingress"
```

## Task 3: Make Configuration Persistence Recoverable

**Files:**
- Modify: `src/main.cpp:77-82`
- Modify: `src/utilities.cpp:21-70,195-267`
- Modify: `test/test_stability/test_main.cpp`

**Step 1: Add failing SPIFFS tests**

Test valid primary loading, invalid primary with valid backup recovery, incomplete temporary file rejection, and preservation of the prior valid file when serialization fails.

Use these paths:

```cpp
constexpr char CONFIG_PATH[] = "/config.json";
constexpr char CONFIG_TEMP_PATH[] = "/config.tmp";
constexpr char CONFIG_BACKUP_PATH[] = "/config.bak";
```

**Step 2: Verify tests fail with direct overwrite behavior**

Expected: interrupted/corrupt primary has no recovery path.

**Step 3: Implement transactional save**

The save sequence must be:

1. Serialize to `/config.tmp` and verify byte count and stream status.
2. Reopen `/config.tmp`, deserialize it, normalize it, and validate it.
3. Remove stale `/config.bak`.
4. Rename valid `/config.json` to `/config.bak`.
5. Rename `/config.tmp` to `/config.json`.
6. Reopen and validate `/config.json` before reporting success.
7. Restore `/config.bak` if final validation fails.

Return `bool` from both load and save functions so callers cannot silently ignore failure.

**Step 4: Stop automatic formatting on every mount failure**

Change boot to attempt `SPIFFS.begin(false)`. If mounting fails, show and log a filesystem error and continue with validated defaults. Format only from the explicit boot-button reset/recovery flow.

**Step 5: Run tests and build**

Expected: all recovery scenarios pass and firmware builds.

**Step 6: Commit**

```powershell
git add src/main.cpp src/utilities.cpp src/utilities.h test/test_stability/test_main.cpp
git commit -m "fix: make configuration writes recoverable"
```

## Task 4: Fix WiFiManager Parameter Lifetime and Runtime Saves

**Files:**
- Modify: `src/networking.h:10-18`
- Modify: `src/networking.cpp:17-163`
- Modify: `src/utilities.cpp:296-335`
- Modify: `src/globals.h:28-36`
- Modify: `src/globals.cpp:10-14`

**Step 1: Add a failing portal lifecycle test or debug assertion**

Expose `initializePortalParameters()` and assert that calling it once returns stable parameter addresses before and after `setupWiFiManager()` returns. Add a test-only accessor under `#ifdef UNIT_TEST` if needed.

**Step 2: Verify current stack-local implementation fails the lifetime requirement**

Expected: parameters cannot be safely inspected after function return.

**Step 3: Give parameters program lifetime**

Create one persistent owner in `networking.cpp`. Prefer static objects registered exactly once:

```cpp
namespace {
WiFiManagerParameter stationParam("station", "Station ID 1", "", 150);
WiFiManagerParameter station2Param("station2", "Station ID 2", "", 150);
WiFiManagerParameter limitParam("limit", "Number of Entries", "", 2);
// Define the remaining fields here with the same lifetime.
bool parametersRegistered = false;
}
```

Populate values with `setValue()` before opening a portal and never call `addParameter()` more than once.

**Step 4: Use a save-parameters callback**

Register `wm.setSaveParamsCallback(...)`. In that callback, parse all persistent parameters into a temporary `Config`, normalize, validate, save transactionally, then atomically assign global `config`. On success, normalize `displayMode` and set `forceRefresh`. On failure, retain the prior config.

**Step 5: Make portal and OTA ownership mutually exclusive**

Reject portal entry while OTA owns port 80. Ensure portal shutdown clears `portalRunning` even after WiFi loss or server error.

**Step 6: Exercise on hardware**

Open and close the portal at least 20 times, save each field, reboot, and verify persisted values and no reset. Record minimum heap before and after the loop.

**Step 7: Commit**

```powershell
git add src/networking.h src/networking.cpp src/utilities.cpp src/globals.h src/globals.cpp
git commit -m "fix: make runtime configuration portal persistent"
```

## Task 5: Replace Stationboard Parsing with a Bounded Snapshot

**Files:**
- Modify: `src/stationboard.h:4-50`
- Modify: `src/stationboard.cpp:1-265`
- Modify: `src/globals.h`
- Modify: `platformio.ini:47-59`
- Modify: `test/test_stability/test_main.cpp`

**Step 1: Write parser tests with fixture strings**

Test complete valid JSON, reordered object members, nested unrelated `name`/`to` keys, truncated JSON, more than ten entries, Unicode escapes, and a response containing a very large ignored `passList`.

The parser contract must be:

```cpp
struct StationboardSnapshot {
    String station;
    std::array<Transport, MAX_TRANSPORTS> rows;
    size_t count = 0;
    unsigned long receivedAt = 0;
};

bool parseStationboard(Stream& input, StationboardSnapshot& output);
```

On failure, `output` must remain unchanged.

**Step 2: Verify tests fail against the SAX listener**

Expected: reordered, nested, truncated, and oversized cases expose current behavior.

**Step 3: Implement filtered ArduinoJson stream parsing**

Use a fixed 8 KB `DynamicJsonDocument` and a small filter containing only:

```text
station.name
stationboard[].name
stationboard[].category
stationboard[].number
stationboard[].to
stationboard[].stop.departure
stationboard[].stop.delay
```

Deserialize directly from `Stream`, reject all errors and `doc.overflowed()`, copy no more than `MAX_TRANSPORTS` rows into a temporary snapshot, validate required fields, and assign output only after the whole document succeeds. Do not perform Unicode replacement; ArduinoJson handles escapes.

**Step 4: Remove the unsafe parser dependency**

Delete `TransportListener` and remove `json-streaming-parser` from `platformio.ini` after connections parsing is migrated in Task 6.

**Step 5: Decouple rendering from fetch memory**

End HTTP and destroy the JSON document before creating a sprite. Check `createSprite()` and preserve the existing frame if it returns null. Render no more than ten rows and avoid a copied `validTransports` vector.

**Step 6: Run tests and build**

Expected: malformed and oversized fixtures fail safely, valid reordered input succeeds, and the firmware builds.

**Step 7: Commit**

```powershell
git add src/stationboard.h src/stationboard.cpp src/globals.h test/test_stability/test_main.cpp
git commit -m "fix: bound stationboard parsing and rendering"
```

## Task 6: Replace Connections Parsing with a Bounded Snapshot

**Files:**
- Modify: `src/connections.h:4-51`
- Modify: `src/connections.cpp:1-247`
- Modify: `platformio.ini:47-59`
- Modify: `test/test_stability/test_main.cpp`

**Step 1: Add connection parser tests**

Cover member reordering, truncated input, empty `products`, nested journeys/pass lists, more than eight results, multi-day duration, and the overnight walking-only case from `docs/plans/2026-09-07-commute-mode-design.md`.

Use this contract:

```cpp
struct ConnectionsSnapshot {
    std::array<Connection, MAX_CONNECTIONS> rows;
    size_t count = 0;
    unsigned long receivedAt = 0;
};

bool parseConnections(Stream& input, ConnectionsSnapshot& output);
```

**Step 2: Verify failures against the current listener**

Expected: partial documents can publish, result count is not internally bounded, and day duration is lost.

**Step 3: Implement a connections filter**

Retain only:

```text
connections[].from.departure
connections[].from.departureTimestamp
connections[].from.delay
connections[].to.arrival
connections[].duration
connections[].transfers
connections[].products
```

Reject truncated/overflowed documents, skip walking-only entries, calculate multi-day duration correctly, and publish only a complete temporary snapshot.

**Step 4: Bound rendering and allocation**

Destroy HTTP/JSON allocations before sprite creation, check `createSprite()`, and render at most `MAX_CONNECTIONS` rows.

**Step 5: Remove JsonStreamingParser completely**

Remove includes, listeners, and the unpinned Git dependency from `platformio.ini`.

**Step 6: Run tests and build**

Expected: all fixtures pass and dependency graph no longer contains `JsonStreamingParser`.

**Step 7: Commit**

```powershell
git add src/connections.h src/connections.cpp src/stationboard.h src/stationboard.cpp platformio.ini test/test_stability/test_main.cpp
git commit -m "fix: bound connections parsing and rendering"
```

## Task 7: Add Bounded HTTP Transactions and Trusted Publication

**Files:**
- Create: `src/http_request.h`
- Create: `src/http_request.cpp`
- Modify: `src/stationboard.cpp`
- Modify: `src/connections.cpp`
- Modify: `src/networking.cpp:165-211`
- Modify: `test/test_stability/test_main.cpp`

**Step 1: Write failing request-policy tests**

Test rejection for excessive `Content-Length`, bytes beyond `MAX_API_RESPONSE_BYTES`, no progress for `HTTP_TIMEOUT`, and total elapsed time beyond a new `HTTP_TOTAL_TIMEOUT`.

**Step 2: Implement one shared bounded input wrapper**

Wrap the HTTP stream so `read()` and `readBytes()` stop when any condition is reached:

```cpp
struct RequestLimits {
    size_t maxBytes;
    unsigned long inactivityMs;
    unsigned long totalMs;
};
```

Track bytes read, last-progress time, and request start using rollover-safe unsigned subtraction. Return a typed result such as `Success`, `HttpError`, `TooLarge`, `TimedOut`, `ParseError`, and `OutOfMemory`.

**Step 3: Force non-chunked responses for direct parsing**

Call `http.useHTTP10(true)` before `begin()`. Reject a known `Content-Length` above the policy before parsing. Keep byte counting because missing or dishonest lengths must not bypass the cap.

**Step 4: Add HTTPS correctly**

Use `WiFiClientSecure` with a maintained CA certificate or ESP certificate bundle for transport and BTC APIs. Do not use insecure TLS. If the transport API cannot be validated in the pinned framework, document that blocker and retain strict body/parser limits rather than silently downgrading.

**Step 5: Make BTC status truthful**

Treat HTTP 200 plus invalid JSON as failure. Do not let BTC success overwrite transport failure status. Return a typed result and aggregate refresh status in `main.cpp`.

**Step 6: Run fixture tests, build, and hardware requests**

Expected: request policies fail closed, all three APIs work with valid TLS, and malformed BTC JSON produces failure.

**Step 7: Commit**

```powershell
git add src/http_request.h src/http_request.cpp src/stationboard.cpp src/connections.cpp src/networking.cpp test/test_stability/test_main.cpp
git commit -m "fix: bound and authenticate network requests"
```

## Task 8: Define Stale-Data and Status Semantics

**Files:**
- Modify: `src/main.cpp:124-210`
- Modify: `src/stationboard.h`
- Modify: `src/stationboard.cpp`
- Modify: `src/connections.h`
- Modify: `src/connections.cpp`
- Modify: `src/utilities.cpp:355-362`
- Modify: `test/test_stability/test_main.cpp`

**Step 1: Add failing state tests**

Verify:

- A failed refresh does not replace the prior snapshot.
- Stationboard rows become stale after a defined age and are visibly marked or cleared.
- Connections follow the approved commute design: retain data only until the first effective departure, then clear it.
- BTC success cannot turn the indicator green after transport failure.
- A partial refresh is never reported as full success.

**Step 2: Introduce explicit refresh state**

Use one aggregate state instead of the last caller painting the indicator:

```cpp
struct RefreshResult {
    FetchResult transport;
    FetchResult btc;
    unsigned long attemptedAt;
};
```

Render status once after all refresh operations. Green means required transport data is fresh; red means failed/expired. BTC is optional and must not hide transport failure.

**Step 3: Implement snapshot publication and expiry**

Maintain current valid snapshots independently from temporary parse output. Use rollover-safe monotonic age for freshness and API timestamps for connection departure expiry. Never label old data as freshly updated.

**Step 4: Run tests and perform failure injection**

Block DNS or disconnect the router after a successful refresh. Confirm stale indication, cache retention, expiry, and recovery after network restoration.

**Step 5: Commit**

```powershell
git add src/main.cpp src/stationboard.h src/stationboard.cpp src/connections.h src/connections.cpp src/utilities.cpp test/test_stability/test_main.cpp
git commit -m "fix: expose and expire stale transport data"
```

## Task 9: Make WiFi Recovery Nonblocking and Boot Offline-Capable

**Files:**
- Modify: `src/main.cpp:63-75,106-133,140-210`
- Modify: `src/networking.cpp:124-141`
- Modify: `src/globals.h`
- Modify: `src/globals.cpp`
- Modify: `test/test_stability/test_main.cpp`

**Step 1: Add scheduler tests**

With a fake clock, test first retry, capped exponential backoff, rollover, successful reset of backoff, refresh scheduling after failure, and continued button/portal service during outages.

**Step 2: Replace blocking reconnect**

Remove the retry `while` and forced disconnect. Model connection attempts with timestamps:

```cpp
struct ReconnectState {
    unsigned long nextAttemptAt = 0;
    unsigned long backoffMs = 1000;
};
```

Call `WiFi.reconnect()` once when due, then return to `loop()`. Cap backoff at five minutes and reset it on `WL_CONNECTED`.

**Step 3: Stop rebooting when initial WiFi is unavailable**

Use a bounded initial configuration attempt. After timeout, continue boot with an offline screen and the nonblocking retry state. Keep explicit user access to the AP/config portal rather than restarting every ten minutes.

**Step 4: Record failed refresh attempts**

Advance attempt timestamps on failure so refresh logic cannot immediately repeat. Continue servicing `button.tick()`, `wm.process()`, OTA when active, and sleep decisions.

**Step 5: Run tests and outage soak**

Boot with the router off, leave it off for 30 minutes, then restore it. Verify no reset loop, bounded retry rate, responsive button, and automatic recovery.

**Step 6: Commit**

```powershell
git add src/main.cpp src/networking.cpp src/globals.h src/globals.cpp test/test_stability/test_main.cpp
git commit -m "fix: recover from WiFi outages without blocking"
```

## Task 10: Make Time, Night Mode, and Sleep Fail Safe

**Files:**
- Modify: `src/main.cpp:14-60,114-177,202-209`
- Modify: `src/utilities.cpp:89-156,364-475`
- Modify: `src/globals.h:69-88`
- Modify: `src/globals.cpp:30-46`
- Modify: `test/test_stability/test_main.cpp`

**Step 1: Add failing time-state tests**

Test invalid initial epoch, failed NTP sync, DST boundaries, equal night times, weekend behavior, temporary wake expiry, `millis()` rollover, and WiFi wake before the requested timer deadline.

**Step 2: Track clock validity independently from rendering**

Set `clockValid` only after a successful NTP update and a plausible epoch. Continue periodic NTP attempts while the display is dark. `isNightModeActive()` must return false until the clock is valid.

**Step 3: Separate NTP update from `drawCurrentTime()`**

Add `updateClock()` to the scheduler. Rendering reads time but does not control whether synchronization occurs.

**Step 4: Check every sleep API result**

Capture and log return values from timer, EXT0, WiFi wake configuration, and `esp_light_sleep_start()`. On failure, restore CPU/backlight state and schedule a bounded retry rather than pretending a normal wake occurred.

**Step 5: Handle wake causes explicitly**

Treat `ESP_SLEEP_WAKEUP_WIFI` separately. If no work is due, sleep for the remaining deadline or disable WiFi wake when it is not required. Preserve timer and button semantics.

**Step 6: Run tests and hardware scenarios**

Test boot without NTP, overnight transition, temporary button wake, router traffic during sleep, and DST transition dates.

**Step 7: Commit**

```powershell
git add src/main.cpp src/utilities.cpp src/globals.h src/globals.cpp test/test_stability/test_main.cpp
git commit -m "fix: make time and sleep transitions fail safe"
```

## Task 11: Make OTA Recoverable and Compatible

**Files:**
- Create: `partitions_ota.csv`
- Modify: `platformio.ini:21`
- Modify: `src/ota.h:8-17`
- Modify: `src/ota.cpp:6-62`
- Modify: `src/main.cpp:150-210`
- Modify: `src/utilities.cpp:296-335`
- Modify: `test/test_stability/test_main.cpp`

**Step 1: Add the dual-slot partition table**

Use a 4 MB layout that fits the currently measured 1,381,061-byte firmware:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x180000,
app1,     app,  ota_1,   0x190000,0x180000,
spiffs,   data, spiffs,  0x310000,0xE0000,
coredump, data, coredump,0x3F0000,0x10000,
```

Point `board_build.partitions` to this file. Add a CI/build check that fails if firmware exceeds either app slot.

**Step 2: Add failing OTA state tests**

Test inactive, awaiting upload, uploading, success, failure, timeout, WiFi loss, night-mode boundary, and portal conflict transitions.

**Step 3: Implement an explicit OTA state machine**

Track activation and progress timestamps. Awaiting-upload mode expires after a short documented window. Uploading mode continues to service OTA across night-mode boundaries. Failure, timeout, or WiFi loss stops the server, clears OTA state, restores CPU/display behavior, and returns to normal scheduling.

**Step 4: Add authentication**

Require credentials from build-time secrets or validated configuration. Never commit a real password. Document the required build environment variables. Keep physical long-press activation as an additional restriction.

**Step 5: Enforce exclusive port ownership**

Portal start must fail while OTA is active; OTA start must fail while the portal is active. Stop and release the active server before transitioning modes.

**Step 6: Verify on hardware**

Test successful OTA, bad image, interrupted upload, no client timeout, router loss, and night-mode transition. Confirm the old image remains bootable after interrupted upload.

**Step 7: Commit**

```powershell
git add partitions_ota.csv platformio.ini src/ota.h src/ota.cpp src/main.cpp src/utilities.cpp test/test_stability/test_main.cpp
git commit -m "fix: make OTA authenticated and recoverable"
```

## Task 12: Pin Dependencies and Complete Soak Verification

**Files:**
- Modify: `platformio.ini:47-59`
- Modify: `README.md:21-30,50-84,129-149`
- Create: `docs/stability-test-procedure.md`

**Step 1: Pin dependencies**

Replace caret ranges and default-branch Git URLs with exact versions or immutable commit SHAs. Record the resolved versions currently shown by the successful build, except remove `JsonStreamingParser` entirely.

**Step 2: Run clean verification**

Run:

```powershell
& "$HOME\.platformio\penv\Scripts\pio.exe" run -t clean
& "$HOME\.platformio\penv\Scripts\pio.exe" run
& "$HOME\.platformio\penv\Scripts\pio.exe" test -e ESP32-2432S028R --without-uploading
```

Expected: clean build and test compilation succeed, OTA slot size is not exceeded, and no dependency resolves from a moving branch.

**Step 3: Run a 24-hour hardware soak**

The procedure must include:

- Normal stationboard and connections refreshes.
- Repeated mode and brightness changes.
- Twenty portal open/save/close cycles.
- Router off for 30 minutes and automatic recovery.
- DNS/API failure and stale-data expiry.
- Night-mode entry, periodic dark synchronization, temporary wake, and exit.
- Successful OTA and interrupted OTA recovery.
- Heap logging at every refresh.

Acceptance thresholds:

```text
Unexpected resets: 0
Largest heap block downward trend after warm-up: none
Invalid/stale data shown as fresh: 0 occurrences
Manual intervention after network/API recovery: none
Portal save persistence failures: 0
```

**Step 4: Update documentation**

Document current connections mode, offline behavior, stale indication, portal controls, OTA credentials/timeout, dual-slot firmware size limit, and exact build commands.

**Step 5: Review the complete branch**

Use the `requesting-code-review` skill. Review every commit and compare the branch against `main`, with priority on allocation lifetime, state transitions, and failure publication.

**Step 6: Commit**

```powershell
git add platformio.ini README.md docs/stability-test-procedure.md
git commit -m "docs: define stable operation verification"
```

## Final Verification Gate

Before claiming completion:

1. Run the verification commands from Task 12 again from a clean build.
2. Confirm the dependency graph contains no `JsonStreamingParser` or moving Git references.
3. Confirm both OTA partitions exist in the generated partition binary/map.
4. Confirm all device tests pass on the target ESP32.
5. Review the 24-hour serial log for resets, allocation failures, repeated reconnects, parser failures, stale publication, and declining `getMaxAllocHeap()`.
6. Inspect `git status`, `git diff main...HEAD`, and every commit before merge or PR.
