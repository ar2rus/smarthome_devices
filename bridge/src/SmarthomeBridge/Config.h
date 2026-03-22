#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace BridgeConfig {

void init();
void loop();
void setupRoutes(AsyncWebServer& server);

bool isApMode();
bool isStaConnected();
const char* timezone();
void sendPage(AsyncWebServerRequest* request);
void forceApMode();

}

#endif
