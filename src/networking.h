#ifndef NETWORKING_H
#define NETWORKING_H

#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "globals.h"
#include "utilities.h"

// Forward declarations
void checkForConfigReset();
void loadConfiguration();
void saveConfiguration();
void saveConfigCallback();
void displayStatus(bool isSuccess);

void setupWiFiManager();
void drawBTC();

// ArduinoJson forward declarations
using ArduinoJson::DynamicJsonDocument;
using ArduinoJson::DeserializationError;

#endif // NETWORKING_H
