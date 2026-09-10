# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

StationBoard is a Swiss public transport real-time departure display running on ESP32-2432S028R (CYD - "Cheap Yellow Display"). It shows departures from two configurable stations, can show connections between them, and displays the BTC price in the footer.

## Build Commands

```powershell
# Build firmware
& "$HOME\.platformio\penv\Scripts\pio.exe" run

# Build and upload to device
& "$HOME\.platformio\penv\Scripts\pio.exe" run -t upload

# Serial monitor (115200 baud)
& "$HOME\.platformio\penv\Scripts\pio.exe" device monitor

# Clean build
& "$HOME\.platformio\penv\Scripts\pio.exe" run -t clean
```

`pio` is not in PATH; use the full path above. The default environment is `ESP32-2432S028R`, and the platform is pinned to `espressif32@6.6.0` for reproducible builds. The OTA build hook reads `OTA_USERNAME` and `OTA_PASSWORD` from the environment and only enables OTA when both are present.

## Architecture

### Source Organization (`src/`)

| File | Purpose |
|------|---------|
| `main.cpp` | Entry point, setup/loop, light sleep management, WiFi reconnection |
| `globals.h/cpp` | Shared configuration struct (`Config`), constants, extern declarations |
| `stationboard.h/cpp` | Bounded stationboard JSON parsing, snapshot expiry, transport rendering |
| `connections.h/cpp` | Bounded connections JSON parsing, departure expiry, journey rendering |
| `networking.h/cpp` | WiFiManager setup, BTC API fetching |
| `utilities.h/cpp` | Time formatting, brightness control, transactional SPIFFS config, night mode |
| `http_request.h` | Shared bounded HTTP response and timeout handling |
| `ota.h/cpp` | Authenticated ElegantOTA firmware update handling |

### Key Data Structures

- `Config` (globals.h): Station IDs, display limit, brightness settings stored in SPIFFS
- `Transport` (stationboard.h): Parsed departure info (name, destination, time, delay)
- `StationboardSnapshot` and `ConnectionsSnapshot`: fixed-capacity cached results used to bound rows and retain valid data during transient failures
- `FetchResult`: distinguishes successful, timed-out, oversized, parse-error, out-of-memory, and HTTP-error fetches
- JSON parsing uses filtered, fixed-capacity ArduinoJson documents and bounded HTTP streams to protect ESP32 RAM

### Hardware Interaction

- **Display**: TFT_eSPI library with ILI9341 driver, 320x240 in landscape
- **Backlight**: PWM on GPIO 21, 5 brightness levels (0, 64, 128, 192, 255)
- **Button**: GPIO 0 (boot button) via OneButton library

**TFT_eSPI Configuration**: Driver settings are defined via build flags in `platformio.ini` (not `User_Setup.h`). A local copy exists in `lib/TFT_eSPI/` which overrides the PlatformIO dependency.

**Display Layout**: Position constants in `globals.h` (`POS_TIME`, `POS_DELAY`, `POS_BUS`, `POS_TO`, `POS_INC`, `POS_FIRST`) control the departure list layout.

**Time Sync**: NTPClient syncs hourly from `pool.ntp.org`; the Timezone library converts UTC to Swiss CET/CEST. Invalid clocks retry every minute.

### Button Actions

- Single click: Cycle brightness; during night mode, temporarily wake the display for 30 seconds
- Double click: Cycle station 1, station 2, and connections mode when enabled
- Triple click: Open or close the WiFi config portal
- Long press (10s): Enter authenticated OTA mode when build-time credentials are present; refused while the portal is active

### Power Management

- Brightness level 4: Light sleep mode with timer and boot-button wake sources
- Brightness levels 0-3: CPU frequency reduced to 80MHz (no light sleep)
- Night mode turns the backlight off, checks the schedule every 5 minutes, and supports a 30-second temporary wake
- Normal refresh interval is 60 seconds; at light-sleep brightness, sleep duration is 57 seconds after a refresh
- `UPDATE_INTERVAL`, `NIGHT_CHECK_INTERVAL`, `SLEEP_DURATION`, and `UPDATE_DURATION` control refresh and sleep timing

### External APIs

- Swiss Transport stationboard API: `transport.opendata.ch/v1/stationboard`
- Swiss Transport connections API: `transport.opendata.ch/v1/connections?from=<station 1>&to=<station 2>&limit=8`
- BTC price: Coinbase endpoint configured by the `getBTCAPI` constant

## Configuration

Settings are stored transactionally in SPIFFS (`/config.json`, with `/config.bak` recovery):
- `station_id` / `station_id2`: Station names, each required and limited to 150 characters
- `limit`: Number of departures to show (1-10)
- `offset`: Stationboard time offset in minutes (-120 to 120)
- `defaultBrightness`: Initial brightness level (0-4)
- `connectionsEnabled`: Enables the third display mode between the two stations
- `nightModeEnabled`, `nightModeStartHour`, `nightModeStartMinute`, `nightModeEndHour`, `nightModeEndMinute`, `nightModeWeekendDisable`: Night-mode schedule and weekend behavior

WiFiManager creates a captive portal named `Stationboard_AP` for initial configuration. The runtime portal is opened or closed with a triple click and times out after 600 seconds.

## Releases

Firmware version is defined in `src/globals.h` as `#define FIRMWARE_VERSION "x.y.z"`. GitHub releases use tag `x.y.z` and attach the three bin files from `.pio/build/ESP32-2432S028R/`: `bootloader.bin`, `firmware.bin`, and `partitions.bin`. `partitions_ota.csv` provides two 1.5 MB OTA application slots.
