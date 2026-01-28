#ifndef NIGHTMODE_H
#define NIGHTMODE_H

#include "globals.h"

// Forward declarations - implementations are in utilities.cpp
bool isNightModeActive();
bool isWeekend();
void checkNightMode();
void enterNightMode();
void exitNightMode();
void handleNightModeButton();
void updateNightModeDisplay();

#endif // NIGHTMODE_H
