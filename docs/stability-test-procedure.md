# Stability Test Procedure

Run this procedure on an ESP32-2432S028R with serial logging enabled. It is a hardware acceptance procedure; successful or interrupted OTA outcomes must be recorded during the test and are not asserted by this document.

## Build And Device Tests

Use the repository's pinned PlatformIO executable:

```powershell
& "$HOME\.platformio\penv\Scripts\pio.exe" run -t clean
& "$HOME\.platformio\penv\Scripts\pio.exe" run
& "$HOME\.platformio\penv\Scripts\pio.exe" test -e ESP32-2432S028R --upload-port COM7
& "$HOME\.platformio\penv\Scripts\pio.exe" project metadata -e ESP32-2432S028R --json-output
```

The dependency graph must contain no `JsonStreamingParser`, default-branch Git URL, caret range, or unpinned package. The exact registry declarations resolve to these build identifiers:

```text
ArduinoJson 6.21.5
NTPClient 3.2.1
TFT_eSPI 2.5.43
Time 1.6.1+sha.a18e50d
Timezone 1.2.4+sha.7cf1425
WiFiManager 2.0.17+sha.b67b782
OneButton 2.6.1+sha.0cebe0c
ElegantOTA 3.1.6+sha.8b99cfc
ESPAsyncWebServer 3.6.0
AsyncTCP 3.3.2
```

`mbed-aluqard/arduino` remains pinned to `0.0.0+sha.3b83fc30bbdf`. Confirm the generated partition table retains both `app0` and `app1` 0x180000-byte (1.5 MB) OTA slots.

## 24-Hour Soak

1. Start a serial capture before the soak. Record free heap and largest allocatable heap at every refresh, along with refresh, WiFi reconnect, parser, stale-data, portal, sleep/wake, and OTA events.
2. Run normal stationboard refreshes for both configured stations. Enable connections mode and verify its normal refreshes, then cycle all available views repeatedly with double-clicks.
3. Cycle every brightness level repeatedly, including light sleep. Exercise night mode entry, periodic dark synchronization, a temporary wake, and scheduled exit.
4. Perform 20 portal open, save, and close cycles. Verify persisted station, display, connections, and night-mode values after each save and after a reboot.
5. Turn the router off for 30 minutes. Verify that the display remains responsive and reconnects without manual action when the router returns.
6. Cause DNS and transport API failures. Verify error status is shown, no invalid data is published as fresh, and stationboard data expires after five minutes. Verify connections disappear at their effective departure time.
7. With `OTA_USERNAME` and `OTA_PASSWORD` set for the build, perform one successful authenticated OTA update. Separately interrupt an upload or remove WiFi; verify the device remains recoverable and returns to normal operation. Do not record either hardware outcome until performed.
8. Continue the normal refresh, mode-change, and brightness-change workload until 24 hours have elapsed. Review the complete serial log.

## Acceptance Thresholds

```text
Unexpected resets: 0
Largest heap block downward trend after warm-up: none
Invalid/stale data shown as fresh: 0 occurrences
Manual intervention after network/API recovery: none
Portal save persistence failures: 0
```

Reject the soak if the serial log shows allocation failures, repeated unsuccessful reconnect loops after recovery, parser failures that publish data, stale data presented as fresh, or a declining largest heap block after warm-up.
