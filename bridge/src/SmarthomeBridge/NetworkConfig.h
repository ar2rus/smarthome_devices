#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace NetworkConfig {

enum State : uint8_t {
  IDLE = 0,
  CONNECTING,
  STA_CONNECTED,
  ACCESS_POINT
};

void begin();
void process();
void setupRoutes(AsyncWebServer& server, bool littleFsAvailable, bool (*transitionAllowed)());

bool stationConnected();
bool accessPointMode();
bool hasSavedSettings();
const char* modeName();
const char* accessPointName();
uint32_t reconnectCount();

}
