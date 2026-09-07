#ifndef FlashFirmware_h
#define FlashFirmware_h

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

namespace FlashFirmware {

void init();
void setupRoutes(AsyncWebServer& server);
void process();
bool legacyUartActive();
void observeApplicationPacket(clunet_packet* packet);
void forwardTransportResult(uint8_t result);

bool isBootloaderUartIsolated(uint8_t address = 0);
bool isTrafficMuted();
void observeBootloaderResponse(clunet_packet* packet);
bool forwardingStartsBootloaderSession(clunet_packet* packet);
void recordForwardedPacket(clunet_packet* packet, IPAddress remoteIP, uint16_t remotePort);
bool shouldForwardMulticastToUart(clunet_packet* packet, IPAddress remoteIP = IPAddress(), uint16_t remotePort = 0);
bool handleBootControlResponse(clunet_packet* packet);

}

#endif
