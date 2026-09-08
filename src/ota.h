#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <WebServer.h>
#include <ElegantOTA.h>

extern int ota_progress_millis;
extern WebServer server;
extern bool otaMode;

// Set OTA_USERNAME and OTA_PASSWORD in the build environment. The build hook
// defines these only when both are present; absent credentials disable OTA.
#ifndef OTA_USERNAME
#define OTA_USERNAME ""
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

constexpr unsigned long OTA_AWAIT_UPLOAD_TIMEOUT_MS = 120000;
constexpr unsigned long OTA_UPLOAD_INACTIVITY_TIMEOUT_MS = 30000;

enum class OtaState { Inactive, AwaitingUpload, Uploading, Succeeded, Failed };

struct OtaStateMachine {
    OtaState current = OtaState::Inactive;
    unsigned long activatedAt = 0;
    unsigned long lastProgressAt = 0;
};

inline bool otaCanStart(bool portalIsRunning, bool credentialsConfigured) {
    return !portalIsRunning && credentialsConfigured;
}

inline void otaActivate(OtaStateMachine& state, unsigned long now) {
    state.current = OtaState::AwaitingUpload;
    state.activatedAt = now;
    state.lastProgressAt = now;
}

inline void otaUploadStarted(OtaStateMachine& state, unsigned long now) {
    state.current = OtaState::Uploading;
    state.lastProgressAt = now;
}

inline void otaUploadProgressed(OtaStateMachine& state, unsigned long now) {
    state.current = OtaState::Uploading;
    state.lastProgressAt = now;
}

inline void otaUploadFinished(OtaStateMachine& state, bool success) {
    state.current = success ? OtaState::Succeeded : OtaState::Failed;
}

inline bool otaMustStop(const OtaStateMachine& state, unsigned long now, bool wifiConnected) {
    if (state.current == OtaState::Inactive || state.current == OtaState::Succeeded) return false;
    if (!wifiConnected || state.current == OtaState::Failed) return true;
    const unsigned long timeout = state.current == OtaState::AwaitingUpload
                                      ? OTA_AWAIT_UPLOAD_TIMEOUT_MS
                                      : OTA_UPLOAD_INACTIVITY_TIMEOUT_MS;
    return now - state.lastProgressAt >= timeout;
}

void onOTAStart();
void onOTAProgress(size_t current, size_t final);
void onOTAEnd(bool success);
void handleLongPress();
void setupOTA();
void handleOTA();
void stopOTA(const char* reason);
bool otaCredentialsConfigured();

#endif // OTA_H
