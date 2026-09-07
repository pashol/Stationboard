#ifndef NETWORKING_H
#define NETWORKING_H

#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <cstring>
#include "globals.h"
#include "utilities.h"

// Forward declarations (Task 3: load/save return bool so callers can detect failure)
void checkForConfigReset();
bool loadConfiguration();
bool saveConfiguration();
void saveConfigCallback();
void displayStatus(bool isSuccess);

void setupWiFiManager();
void drawBTC();

// Refresh the portal parameters' displayed values from the live `config`.
// Must be called before each portal open (boot autoConnect + runtime
// startWebPortal) so the portal never shows stale defaults.
void refreshPortalParameters();

// Task 4: parameters are registered exactly once from program-lifetime
// (anonymous-namespace static) storage in networking.cpp.
void registerPortalParametersOnce();

// Task 4: runtime verifier for the program-lifetime property above. First
// call records the 15 parameter addresses; subsequent calls return false if
// any address changed (impossible for statics) — exposed so Task 12 soak /
// Serial checks can observe stability on hardware.
bool portalParametersAreStable();

// Task 4: stable lookup over parameters registered with the global `wm`.
// Returns the registered parameter with matching id, or nullptr.
// Defined inline here (not in networking.cpp) so the device-test TU — which
// links no src/*.cpp — uses the identical definition without any
// UNIT_TEST guard; firmware TUs share it via the same header.
inline WiFiManagerParameter* getPortalParameter(const char* id) {
    if (id == nullptr) return nullptr;
    WiFiManagerParameter** params = wm.getParameters();
    if (params == nullptr) return nullptr;
    int count = wm.getParametersCount();
    for (int i = 0; i < count; i++) {
        WiFiManagerParameter* p = params[i];
        if (p != nullptr && p->getID() != nullptr && std::strcmp(p->getID(), id) == 0) {
            return p;
        }
    }
    return nullptr;
}

// ArduinoJson forward declarations
using ArduinoJson::DynamicJsonDocument;
using ArduinoJson::DeserializationError;

#endif // NETWORKING_H
