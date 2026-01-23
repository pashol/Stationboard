#include "globals.h"
#include <WiFiUDP.h>

Config config;
bool isFirstStation = true; // Start with first station

const char* DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

TFT_eSPI tft;
WiFiManager wm;
bool shouldSaveConfig = false;
bool portalRunning = false;
int numClicks = 0;

const long timeOffset = 0; // UTC (DST handled by Timezone library in utilities.cpp)
const unsigned long HTTP_TIMEOUT = 10000;
const char* getBTCAPI = "https://api.coinbase.com/v2/prices/BTC-USD/spot";

const int BUTTON_PIN = 0;
const int BRIGHTNESS_LEVELS[] = {0, 64, 128, 192, 255};
const int NUM_LEVELS = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);
int currentBrightnessIndex = 4;

const int PWM_CHANNEL = 0; 
const int PWM_FREQ = 5000;
const int PWM_RESOLUTION = 8;
const int BACKLIGHT_PIN = TFT_BL;

unsigned long temporaryOnStart = 0;
const unsigned long TEMP_ON_DURATION = 300000;

unsigned long previousMillis = 0;
const unsigned long SLEEP_DURATION = 57000000;    // 57 Sekunden
const unsigned long UPDATE_INTERVAL = 60000; // 60 seconds between updates
const unsigned long UPDATE_DURATION = 5000; // 5 seconds for update to complete

OneButton button(BUTTON_PIN, true);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", timeOffset);
