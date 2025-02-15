# Stationboard

🚂 Swiss Public Transport Display for Home | CHF 30

Never miss your train again! This WiFi-enabled real-time display shows departures from two stations of your choice. Perfect for commuters:

- Setup via smartphone
- 4 brightness levels 
- Energy efficient (<1W)
- Compact design
- Live updates of Swiss public transport data

Easy installation: Just needs WiFi (2.4 GHz) and power outlet.

Bring the train station display to your home or office! Get accurate departure times for buses, trains, and boats right where you need them.

![stationboard](img/stationboard.jpg)

# Implemented Features
- [x] Second Station (double click)
- [x] OTA Firmware Update (long press)
- [x] Distance to station as parameter
- [x] Refactring code
- [x] Power savings
- [x] Fixing sprite issues (low memory)
- [ ] Night mode
- [ ] From-To-Stationboard
- [ ] OTA over internet



## Power considerations
Based on the provided power consumption data for the ESP32 and the LCD backlight, we can estimate the power draw for each brightness level as follows:

### ESP32 Power Consumption

Normal mode @ 240MHz: ~160-170mA

Normal mode @ 80MHz: ~30-40mA

Light sleep: ~0.8mA

### LCD Backlight Power Consumption

Max brightness (255): Could draw 50-100mA or more

Lower brightness levels will draw proportionally less


### Estimated Power Draw for Each Brightness Level

#### Level 0 (0)

ESP32: 30-40mA (80MHz, no light sleep)

Backlight: 0mA

Total: 30-40mA

#### Level 1 (64)

ESP32: 30-40mA (80MHz, no light sleep)

Backlight: ~12.5-25mA (assuming linear scaling from max brightness)

Total: 42.5-65mA

#### Level 2 (128)

ESP32: 30-40mA (80MHz, no light sleep)

Backlight: ~25-50mA

Total: 55-90mA

#### Level 3 (192)

ESP32: 30-40mA (80MHz, no light sleep)

Backlight: ~37.5-75mA

Total: 67.5-115mA

#### Level 4 (255):

ESP32: ~0.8mA (light sleep) 

Backlight: 50-100mA

Total: 50.8-100.8mA


These estimates assume linear scaling of backlight power consumption with PWM duty cycle. The actual power draw may vary based on the specific characteristics of the LCD and the ESP32-2432S028R.
