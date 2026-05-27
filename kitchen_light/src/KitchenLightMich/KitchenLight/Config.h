#ifndef CONFIG_H
#define CONFIG_H

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "KitchenLight.h"

extern PersistedSettings persistedSettings;
extern WifiState wifiState;
extern unsigned long rebootScheduledAt;
extern unsigned long lastMqttReconnect;
extern bool littleFsAvailable;
extern char accessPointSsid[32];

void initializeConfig();
bool hasSavedWiFiSettings();
bool hasConfiguredWiFiSettings();
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
void updateWiFiState(unsigned long nowMs);
void scheduleReboot();
const char* wifiStateLabel();
String wifiModeId();
void appendConfigPayload(DynamicJsonDocument& doc);
void setupConfigApiRoutes(AsyncWebServer& server);

#endif
