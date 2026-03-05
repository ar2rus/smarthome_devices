#ifndef FlashFirmware_h
#define FlashFirmware_h

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

namespace FlashFirmware {

void init();
void setupRoutes(AsyncWebServer& server);
void process();

bool isBootloaderUartIsolated(uint8_t address = 0);
bool isTrafficMuted();
void touchBootloaderActivity(uint8_t address);
bool shouldForwardMulticastToUart(clunet_packet* packet);
bool handleBootControlResponse(clunet_packet* packet);

}

#endif
