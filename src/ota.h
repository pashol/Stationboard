#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <WebServer.h>
#include <ElegantOTA.h>

extern int ota_progress_millis;
extern WebServer server;
extern bool otaMode;

void onOTAStart();
void onOTAProgress(size_t current, size_t final);
void onOTAEnd(bool success);
void handleLongPress();
void setupOTA();
void handleOTA();

#endif // OTA_H
