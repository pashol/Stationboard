# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

StationBoard is a Swiss public transport real-time departure display running on ESP32-2432S028R (CYD - "Cheap Yellow Display"). It shows departures from two configurable stations using the Swiss Transport API, with BTC price display in the footer.

## Build Commands

```bash
# Build firmware
pio run

# Build and upload to device
pio run -t upload

# Serial monitor (115200 baud)
pio device monitor

# Clean build
pio run -t clean
```

Platform is pinned to `espressif32@6.6.0` for reproducible builds.

## Architecture

### Source Organization (`src/`)

| File | Purpose |
|------|---------|
| `main.cpp` | Entry point, setup/loop, light sleep management, WiFi reconnection |
| `globals.h/cpp` | Shared configuration struct (`Config`), constants, extern declarations |
| `stationboard.h/cpp` | JSON streaming parser (`TransportListener`), transport data rendering |
| `networking.h/cpp` | WiFiManager setup, BTC API fetching |
| `utilities.h/cpp` | Time formatting, brightness control, SPIFFS config load/save |
| `ota.h/cpp` | ElegantOTA firmware update handling |

### Key Data Structures

- `Config` (globals.h): Station IDs, display limit, brightness settings stored in SPIFFS
- `Transport` (stationboard.h): Parsed departure info (name, destination, time, delay)
- `TransportListener`: JSON streaming parser implementing `JsonListener` interface - uses streaming to avoid loading full API response into memory (important for ESP32 RAM constraints)

### Hardware Interaction

- **Display**: TFT_eSPI library with ILI9341 driver, 320x240 in landscape
- **Backlight**: PWM on GPIO 21, 5 brightness levels (0, 64, 128, 192, 255)
- **Button**: GPIO 0 (boot button) via OneButton library

**TFT_eSPI Configuration**: Driver settings are defined via build flags in `platformio.ini` (not `User_Setup.h`). A local copy exists in `lib/TFT_eSPI/` which overrides the PlatformIO dependency.

**Display Layout**: Position constants in `globals.h` (`POS_TIME`, `POS_DELAY`, `POS_BUS`, `POS_TO`, `POS_INC`, `POS_FIRST`) control the departure list layout.

**Time Sync**: NTPClient updates hourly with UTC+1 offset (Switzerland).

### Button Actions

- Single click: Cycle brightness
- Double click: Switch between station 1 and 2
- Multi-click: Start WiFi config portal
- Long press (10s): Enter OTA update mode

### Power Management

- Brightness level 4: Light sleep mode with timer/button/WiFi wake sources
- Brightness levels 0-3: CPU frequency reduced to 80MHz (no sleep)
- `UPDATE_INTERVAL` controls refresh cycle timing

### External APIs

- Swiss Transport API: `transport.opendata.ch/v1/stationboard`
- BTC price: Configured via `getBTCAPI` constant

## Configuration

Settings stored in SPIFFS (`/config.json`):
- `stationId` / `stationId2`: Station names
- `limit`: Number of departures to show
- `defaultBrightness`: Initial brightness level (0-4)

WiFiManager creates a captive portal named "Stationboard-AP" for initial configuration.
