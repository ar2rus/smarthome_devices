#ifndef CONFIG_H
#define CONFIG_H

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "RelayController.h"

extern PersistedSettings persistedSettings;
extern WifiState wifiState;
extern unsigned long rebootScheduledAt;
extern char accessPointSsid[32];
extern String currentTimeZoneId;

void initializeConfig();
bool hasSavedWiFiSettings();
bool loadPersistedSettings();
bool savePersistedSettings(
  const String& ssid,
  const String& password,
  bool useStaticIp,
  const String& ip,
  const String& gateway,
  const String& subnet,
  const String& dns,
  const String& mqttHost,
  uint16_t mqttPort,
  const String& mqttUser,
  const String& mqttPassword
);
void loadTimeSettingsFromFile();
void saveTimeSettingsToFile();
bool applyTimeZoneById(const String& id);
void appendTimePayload(DynamicJsonDocument& doc);
void appendConfigPayload(DynamicJsonDocument& doc);
void updateWiFiState(unsigned long nowMs);
void scheduleReboot();
const char* wifiStateLabel();
String wifiModeId();
void setupConfigApiRoutes(AsyncWebServer& server);

#endif
