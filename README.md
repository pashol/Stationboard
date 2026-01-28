# StationBoard

Swiss Public Transport Display for Home

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/H2H21VNZU)

Never miss your train again! This WiFi-enabled real-time display shows departures from two stations of your choice. Perfect for commuters:

- Setup via smartphone
- 4 brightness levels
- Energy efficient (<1W)
- Compact design
- Live updates of Swiss public transport data

Easy installation: Just needs WiFi (2.4 GHz) and USB power.

Bring the train station display to your home or office! Get accurate departure times for buses, trains, and boats right where you need them.

![StationBoard Display](img/stationboard.jpg)

## Features

- **Real-time departures** from Swiss public transport (trains, buses, trams, boats)
- **Two stations** - switch between two configurable stations with a double-click
- **5 brightness levels** including a power-saving sleep mode
- **BTC price ticker** in the footer
- **OTA firmware updates** - update wirelessly via web browser
- **WiFi configuration portal** - easy setup via smartphone
- **Automatic time sync** with NTP (handles DST)
- **Night mode** - automatic power saving from 22:00 to 06:00

## Hardware Requirements

- **ESP32-2432S028R** (CYD - "Cheap Yellow Display")
  - ESP32 with integrated 2.8" ILI9341 TFT display (320x240)
  - Built-in boot button for controls
- USB power supply (5V)
- 2.4 GHz WiFi network

## Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension) Only for building from source
- USB cable for initial flashing

### Flash 
Use [this](https://esptool.spacehuhn.com) to flash the files from the release

## Configuration

### Initial Setup

1. Power on the device
2. If no WiFi is configured, it creates an access point named **"Stationboard-AP"**
3. Connect to this AP with your smartphone
4. A captive portal opens where you can:
   - Select your WiFi network and enter credentials
   - Set **Station 1** and **Station 2** names (e.g., "Zürich HB", "Bern")
   - Configure the number of departures to display
   - Set default brightness level

### Reconfiguring WiFi

Multi-click the button to re-enter the WiFi configuration portal.

## Usage

### Button Controls

| Action | Function |
|--------|----------|
| **Single click** | Cycle through brightness levels (0-4) |
| **Double click** | Switch between Station 1 and Station 2 |
| **Multi-click** | Enter WiFi configuration portal |
| **Long press (10s)** | Enter OTA firmware update mode |

### OTA Updates

1. Long press the button for 10 seconds
2. The device enters OTA mode and displays its IP address
3. Open a web browser and navigate to `http://<device-ip>/update`
4. Upload the new firmware binary

## Power Considerations

Based on the provided power consumption data for the ESP32 and the LCD backlight, we can estimate the power draw for each brightness level as follows:

### ESP32 Power Consumption

- Normal mode @ 240MHz: ~160-170mA
- Normal mode @ 80MHz: ~30-40mA
- Light sleep: ~0.8mA

### LCD Backlight Power Consumption

- Max brightness (255): Could draw 50-100mA or more
- Lower brightness levels will draw proportionally less

### Estimated Power Draw for Each Brightness Level

| Level | PWM Value | Backlight | ESP32 Mode | Total Draw |
|-------|-----------|-----------|------------|------------|
| 0 | 0 | 0mA | 80MHz (30-40mA) | 30-40mA |
| 1 | 64 | ~12.5-25mA | 80MHz (30-40mA) | 42.5-65mA |
| 2 | 128 | ~25-50mA | 80MHz (30-40mA) | 55-90mA |
| 3 | 192 | ~37.5-75mA | 80MHz (30-40mA) | 67.5-115mA |
| 4 | 255 | 50-100mA | Light sleep (~0.8mA) | 50.8-100.8mA |

These estimates assume linear scaling of backlight power consumption with PWM duty cycle. The actual power draw may vary based on the specific characteristics of the LCD and the ESP32-2432S028R.

### Build and Upload

```bash
# Clone the repository
git clone https://github.com/pashol/Stationboard.git
cd Stationboard

# Build firmware
pio run

# Upload to device (connect via USB)
pio run -t upload

# Monitor serial output (optional)
pio device monitor
```

## Architecture

```
src/
├── main.cpp          # Entry point, setup/loop, sleep management
├── globals.h/cpp     # Configuration struct, constants
├── stationboard.h/cpp# JSON streaming parser, display rendering
├── networking.h/cpp  # WiFiManager, BTC API
├── utilities.h/cpp   # Time formatting, brightness, SPIFFS config
└── ota.h/cpp         # ElegantOTA handling
```

### Key Libraries

- **TFT_eSPI** - Display driver
- **WiFiManager** - Captive portal for WiFi setup
- **ArduinoJson** - JSON parsing
- **json-streaming-parser** - Memory-efficient API response parsing
- **ElegantOTA** - Web-based firmware updates
- **OneButton** - Button handling
- **Timezone** - DST-aware time handling

## APIs Used

- [Swiss Transport API](https://transport.opendata.ch/) - Real-time departure data
- BTC price API - Cryptocurrency ticker

## Roadmap

- [x] Second station support (double click)
- [x] OTA firmware updates (long press)
- [x] Distance to station as parameter
- [x] Code refactoring
- [x] Power savings
- [x] Fixing sprite issues (low memory)
- [x] Night mode (automatic time-based activation, 22:00-06:00)
- [ ] From-To-Stationboard
- [ ] OTA over internet

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## License

This project is open source. Please check the repository for license details.

## Acknowledgments

- [Swiss Transport API](https://transport.opendata.ch/) for providing free public transport data
- The ESP32 and Arduino communities
