/**
 * Use 3.1.2 esp8266 core
 * lwip v2 Higher bandwidth; CPU 80 MHz
 * Flash size: 4M (FS: 1Mb / OTA: 1019 Kb) !!!
 * 
 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include <LittleFS.h>
#include <SPIFFSEditor.h>

#include <ClunetMulticast.h>
#include <MessageDecoder.h>
#include <HexUtils.h>

#include "ClunetCommands.h"
#include "ClunetDevices.h"

#include "SmarthomeBridge.h"
#include "FlashFirmware.h"
#include "BridgeTransport.h"
//#include "Credentials.h"

#ifdef DEFAULT_MAX_SSE_CLIENTS
  #undef DEFAULT_MAX_SSE_CLIENTS 
  #define DEFAULT_MAX_SSE_CLIENTS 10
#endif

const char *ssid = "gNet-aux";
const char *pass = "medvedAn86B";

IPAddress ip(192, 168, 50, 243);     //Node static IP
IPAddress gateway(192, 168, 50, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsAddr(192, 168, 50, 1);

AsyncWebServer server(80);
AsyncEventSource events("/events");

ClunetMulticast clunet(CLUNET_ID, CLUNET_DEVICE);

uint32_t event_id = 0;
uint32_t bootId = 0;
uint32_t uartDrops = 0, multicastDrops = 0, eventDrops = 0, invalidUart = 0, uartCrcErrors = 0, uartTimeouts = 0;
uint32_t uartOverflows = 0, minHeap = UINT32_MAX, maxLoopMicros = 0, reconnects = 0;
uint32_t uartLastRxAt = 0;
uint16_t routingBudget = 2000;
uint32_t suppressedEvents = 0, unobservedEvents = 0, hardwareOverruns = 0, hardwareRxErrors = 0;
#define EVENTS_QUEUE_MAX_LENGTH 64
#define UART_MESSAGES_PER_LOOP 8
#define MULTICAST_MESSAGES_PER_LOOP 8
#define EVENT_MESSAGES_PER_LOOP 4
#define DISCOVERY_RESPONSE_TIMEOUT_MS 1500

api_request_state* retainApiRequestState(api_request_state* state);
void releaseApiRequestState(api_request_state* state);
AsyncWebServerRequest* getApiRequestWebRequest(api_request_state* state);
void deleteApiRequest(api_request* request);
void freeApiResponse();

PacketQueue uartQueue = PacketQueue([](clunet_packet *m){ delete[] reinterpret_cast<char*>(m); });
PacketQueue multicastQueue = PacketQueue([](clunet_packet *m){ delete[] reinterpret_cast<char*>(m); });

BoundedQueue<ts_clunet_packet*, EVENTS_QUEUE_MAX_LENGTH> eventsQueue = BoundedQueue<ts_clunet_packet*, EVENTS_QUEUE_MAX_LENGTH>([](ts_clunet_packet *m){ free(m); });

BoundedQueue<api_request*, 8> apiRequestsQueue = BoundedQueue<api_request*, 8>(deleteApiRequest);
api_response* apiResponse = NULL;
bool clunetConnected = false;
uint32_t discoveryResponsesSniffed = 0;
uint32_t discoveryResponsesMatchedActiveRequest = 0;
uint32_t discoveryResponsesReturnedToHttp = 0;
uint32_t discoveryResponseHttpCallbacks = 0;
uint32_t discoveryResponseActiveRequestId = 0;
unsigned long discoveryResponseLastSniffedAt = 0;
unsigned long discoveryResponseLastMatchedAt = 0;
unsigned long discoveryResponseLastReturnedAt = 0;
unsigned long discoveryResponseLastHttpCallbackAt = 0;

#define UART_MESSAGE_CODE_CLUNET 1
#define UART_MESSAGE_CODE_FIRMWARE 2
#define UART_MESSAGE_CODE_DEBUG 10

const char UART_MESSAGE_PREAMBULE[] = {0xC9, 0xE7};
extern volatile uint16_t uart_rx_data_len;
extern bool uart_rx_overflow;

uint8_t uart_can_send(uint8_t length){
  return length <= 76 && Serial.availableForWrite() >= length + 5;
}

uint8_t uart_can_send(clunet_packet* packet){
  return uart_can_send(packet->len());
}

uint8_t uart_send_message(char code, char* data, uint8_t length){
  if (!uart_can_send(length)){
    return 0;
  }
  
  uint8_t buf_length = length + 2;
  
  char buf[buf_length];
  buf[0] = length + 3;
  buf[1] = code;
  
  if (length){
    memcpy((void*)(buf + 2), data, length);
  }

  Serial.write((char*)UART_MESSAGE_PREAMBULE, 2);
  Serial.write(buf, buf_length);
  Serial.write(check_crc(buf, buf_length));

  return 1;
}

uint8_t uart_send_message(clunet_packet* packet){
  return uart_send_message(UART_MESSAGE_CODE_CLUNET, (char*)packet, packet->len());
}

bool queuePacket(PacketQueue& queue, clunet_packet* packet, uint32_t& drops){
  if (queue.full() || ESP.getFreeHeap() < 12000) { ++drops; return false; }
  clunet_packet* copy = packet->copy();
  if (!queue.add(copy, millis(), routingBudget)) { delete[] reinterpret_cast<char*>(copy); ++drops; return false; }
  return true;
}

bool toWire(uint8_t destination) { return destination < 0x80 || destination == CLUNET_ADDRESS_BROADCAST; }

bool queueUart(clunet_packet* packet, IPAddress remoteIP = IPAddress(), uint16_t remotePort = 0) {
  const uint8_t maxSize = packet->command == CLUNET_COMMAND_BOOT_CONTROL ? 68 : 64;
  if (packet->size > maxSize || !FlashFirmware::shouldForwardMulticastToUart(packet, remoteIP, remotePort)) { ++uartDrops; return false; }
  if (FlashFirmware::forwardingStartsBootloaderSession(packet)) {
    uartDrops += uartQueue.length(); uartQueue.clear();
  }
  if (!queuePacket(uartQueue, packet, uartDrops)) return false;
  FlashFirmware::recordForwardedPacket(packet, remoteIP, remotePort);
  return true;
}

void observePacket(clunet_packet* packet) {
  ++event_id;
  if (packet->command == CLUNET_COMMAND_DISCOVERY_RESPONSE) {
    ++discoveryResponsesSniffed; discoveryResponseLastSniffedAt = millis();
  }
  // A reconnect starts with fresh events, never queued historical button presses.
  if (FlashFirmware::isTrafficMuted()) { ++suppressedEvents; return; }
  if (!events.count()) { ++unobservedEvents; return; }
  if (eventsQueue.full() || ESP.getFreeHeap() < 12000) { ++eventDrops; return; }
  ts_clunet_packet* tp = static_cast<ts_clunet_packet*>(malloc(sizeof(ts_clunet_packet) + packet->len()));
  if (!tp) { ++eventDrops; return; }
  timeval tv; gettimeofday(&tv, nullptr);
  tp->sequence = event_id;
  tp->timestamp_sec = tv.tv_sec; tp->timestamp_ms = tv.tv_usec / 1000;
  packet->copy(tp->packet);
  if (!eventsQueue.add(tp, millis())) { free(tp); ++eventDrops; }
}

size_t routeLocalPacket(clunet_packet* packet) {
  if (FlashFirmware::isTrafficMuted()) return 0;
  if (toWire(packet->dst)) {
    if (!queueUart(packet)) return 0;
    // Publication is best effort and must not gate local CLUNET delivery.
    if (clunetConnected) queuePacket(multicastQueue, packet, multicastDrops);
  } else {
    if (!clunetConnected || !queuePacket(multicastQueue, packet, multicastDrops)) return 0;
  }
  observePacket(packet);
  return packet->len();
}

api_request_state* createApiRequestState(AsyncWebServerRequest* webRequest){
  api_request_state* state = (api_request_state*)malloc(sizeof(api_request_state));
  if (state){
    state->webRequest = webRequest;
    state->refs = 1;
  }
  return state;
}

api_request_state* retainApiRequestState(api_request_state* state){
  if (state != NULL){
    state->refs++;
  }
  return state;
}

void releaseApiRequestState(api_request_state* state){
  if (state != NULL){
    if (state->refs > 0){
      state->refs--;
    }
    if (!state->refs){
      free(state);
    }
  }
}

AsyncWebServerRequest* getApiRequestWebRequest(api_request_state* state){
  return state != NULL ? state->webRequest : NULL;
}

void deleteApiRequest(api_request* request){
  if (request != NULL){
    releaseApiRequestState(request->state);
    free(request);
  }
}

void freeApiResponse(){
  discoveryResponseActiveRequestId = 0;
  if (apiResponse != NULL){
    releaseApiRequestState(apiResponse->state);
    free(apiResponse);
    apiResponse = NULL;
  }
}


void _request(AsyncWebServerRequest* webRequest, uint8_t address, uint8_t command, char* data, uint8_t size,
                int responseFilterCommand, long responseTimeout, bool _infoRequest, String _infoRequestId){
    if (FlashFirmware::isTrafficMuted() || apiRequestsQueue.full() || ESP.getFreeHeap() < 12000) {
      webRequest->send(503, "text/plain", "bridge busy"); return;
    }
    if (size > (toWire(address) ? 64 : CLUNET_PACKET_DATA_SIZE) || responseTimeout < 1 || responseTimeout > 5000 ||
        responseFilterCommand < -1 || responseFilterCommand > 255 || _infoRequestId.length() >= 64) {
      webRequest->send(400, "text/plain", "invalid request"); return;
    }
    webRequest->client()->setRxTimeout(10);
    api_request_state* requestState = createApiRequestState(webRequest);
    if (!requestState){
      webRequest->send(503, "text/plain", "busy");
      return;
    }

    api_request* ar = (api_request*)malloc(sizeof(api_request) + size);
    if (!ar){
      releaseApiRequestState(requestState);
      webRequest->send(503, "text/plain", "busy");
      return;
    }
    ar->state = requestState;
    ar->info = _infoRequest;
    if (_infoRequest){
      strcpy(ar->infoId, _infoRequestId.c_str());
    }
    ar->address = address;
    ar->command = command;
    ar->responseFilterCommand = responseFilterCommand;
    ar->responseTimeout = responseTimeout;
    ar->size = size;
    if (size) memcpy(ar->data, data, size);
    apiRequestsQueue.add(ar, millis());

    api_request_state* disconnectState = retainApiRequestState(requestState);
    webRequest->onDisconnect([disconnectState](){
      if (disconnectState != NULL){
        disconnectState->webRequest = NULL;
        releaseApiRequestState(disconnectState);
      }
    });
}

void _request(AsyncWebServerRequest* webRequest, uint8_t address, uint8_t command, char* data, uint8_t size,
                int responseFilterCommand, long responseTimeout){
    _request(webRequest, address, command, data, size, responseFilterCommand, responseTimeout, false, "");
}

int int_param(AsyncWebServerRequest* request, String param){
    return request->getParam(param)->value().toInt();
}

int address_param(AsyncWebServerRequest* request){
    return int_param(request, "a");
}

String id_param(AsyncWebServerRequest* request){
    return request->getParam("id")->value();
}

void api_200(AsyncWebServerRequest* request){
    request->send(200, "text/plain", "{\"body\": \"OK\"}");
}

void api_400(AsyncWebServerRequest* request, String params){
    request->send(400, "text/plain", request->url() + "?" + params);
}

void api_dimmer_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&id=channel_id&value=[0:100]");
}

bool _api_dimmer(int address, int channel_id, int value){
    char data[] = {(char)channel_id, (char)map(value, 0, 100, 0, 255)};
    return clunet.send(address, CLUNET_COMMAND_DIMMER, data, 2) != 0;
}

void api_dimmer(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("id") || !request->hasParam("value")){
        api_dimmer_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>100 || address_param(request) < 0 || address_param(request) > 255 || id_param(request).toInt() < 0 || id_param(request).toInt() > 255){
        api_dimmer_400(request);
        return;
    }

    if (_api_dimmer(address_param(request), id_param(request).toInt(), value)) api_200(request);
    else request->send(503, "text/plain", "bridge busy");
}

void api_switch_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&id=channel_id&value=[0:1]");
}

bool _api_switch(int address, int channel_id, int value){
    char data[] = {(char)value, (char)channel_id};
    return clunet.send(address, CLUNET_COMMAND_SWITCH, data, 2) != 0;
}

void api_switch(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("id") || !request->hasParam("value")){
        api_switch_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>1 || address_param(request) < 0 || address_param(request) > 255){
        api_switch_400(request);
        return;
    }

    if (_api_switch(address_param(request), id_param(request).toInt(), value)) api_200(request);
    else request->send(503, "text/plain", "bridge busy");
}

void api_fan_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&value=[0:1]");
}

bool _api_fan(int address, int value){
    char data = value ? 4 : 3;
    return clunet.send(address, CLUNET_COMMAND_FAN, &data, 1) != 0;
}

void api_fan(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("value")){
        api_fan_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>1 || address_param(request) < 0 || address_param(request) > 255){
        api_fan_400(request);
        return;
    }

    if (_api_fan(address_param(request), value)) api_200(request);
    else request->send(503, "text/plain", "bridge busy");
}

void api_door_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&value=[0:1]");
}

bool _api_door(int address, int value){
    return clunet.send(address, CLUNET_COMMAND_DOOR, (char*)&value, 1) != 0;
}

void api_door(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("value")){
        api_door_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>1 || address_param(request) < 0 || address_param(request) > 255){
        api_fan_400(request);
        return;
    }

    if (_api_door(address_param(request), value)) api_200(request);
    else request->send(503, "text/plain", "bridge busy");
}

void info_switch_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&id=channel_id");
}

void info_switch(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("id")){
        info_switch_400(request);
        return;
    }

    char data = 0xFF;
    _request(request, address_param(request), CLUNET_COMMAND_SWITCH, &data, 1, CLUNET_COMMAND_SWITCH_INFO, 250, true, id_param(request));
}

void info_fan_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address");
}

void info_fan(AsyncWebServerRequest* request){
    if (!request->hasParam("a")){
        info_fan_400(request);
        return;
    }

    char data = 0xFF;
    _request(request, address_param(request), CLUNET_COMMAND_FAN, &data, 1, CLUNET_COMMAND_FAN_INFO, 250, true, "");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("\n\nHello");
  
  Serial.begin(38400, SERIAL_8N1);
  Serial.swap();
  Serial.setRxBufferSize(512);
  bootId = ESP.random();
  FlashFirmware::init();

  if (!LittleFS.begin()) {
    Serial1.println("LittleFS mount failed");
  }

  WiFi.mode(WIFI_STA);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.config(ip, gateway, subnet, dnsAddr);
  WiFi.begin(ssid, pass);

  pinMode(LED_BLUE_PORT, OUTPUT);  
  analogWrite(LED_BLUE_PORT, 12);
  
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  ArduinoOTA.setHostname("smarthome-bridge");
  ArduinoOTA.onStart([]() {
    Serial1.println("ArduinoOTA start update");
    if (ArduinoOTA.getCommand() == U_FS) {
      LittleFS.end();
    }
  });
  ArduinoOTA.begin();

  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  clunet.ignoreOwnDatagrams(true);
  clunet.onRouteSend(routeLocalPacket);
  {
    clunet.onPacketSniffFrom([](clunet_packet* packet, IPAddress remoteIP, uint16_t remotePort){
      if (CLUNET_MULTICAST_DEVICE(packet->src) && toWire(packet->dst)) queueUart(packet, remoteIP, remotePort);
      observePacket(packet);
    });

    clunet.onResponseReceived([](int requestId, LinkedList<clunet_response*>* responses){
      if (requestId == (int)discoveryResponseActiveRequestId){
        discoveryResponseHttpCallbacks++;
        discoveryResponseLastHttpCallbackAt = millis();
      }

      if (apiResponse != NULL && apiResponse->requestId == requestId){
        AsyncWebServerRequest* webRequest = getApiRequestWebRequest(apiResponse->state);
        if (webRequest != NULL){
          DynamicJsonDocument doc(4196);
          JsonObject root = doc.to<JsonObject>();
          root["id"] = requestId;
          JsonArray docArray;
          if (!apiResponse->info){
            docArray = root.createNestedArray("responses");
          }

          for(auto i = responses->begin(); i != responses->end(); ++i){
            clunet_response* response = *i;
            if (requestId == response->requestId){
              if (requestId == (int)discoveryResponseActiveRequestId &&
                  response->packet->command == CLUNET_COMMAND_DISCOVERY_RESPONSE){
                discoveryResponsesReturnedToHttp++;
                discoveryResponseLastReturnedAt = millis();
              }
              if (apiResponse->info){
                fillValueData(root, response->packet, apiResponse->infoId);
                break;
              }else{
                fillMessageJsonObject(docArray.createNestedObject(), 0, 0, response->packet);
              }
            }
          }

          String json;
          serializeJson(doc, json);
          bool hasResponse = !responses->isEmpty();
          webRequest->send(doc.overflowed() ? 503 : (hasResponse ? 200 : 504), "application/json", json);
        }
        freeApiResponse();
      }
    });
  }

  server.on("/command", HTTP_GET, [](AsyncWebServerRequest* request) {
       if (!request->hasParam("c")){
        request->send(400, "text/plain", "/command?c=command_id[&a=device_address][&d=hex_data]");
        return;
      }
      int command = request->getParam("c")->value().toInt();
      int address = request->hasParam("a") ? request->getParam("a")->value().toInt() : CLUNET_ADDRESS_BROADCAST;

      int dataLen = 0;
      char data[CLUNET_PACKET_DATA_SIZE];
      if (request->hasParam("d")){
        String hexData = request->getParam("d")->value();
        dataLen = hexStringToCharArray(data, hexData.c_str(), hexData.length(), sizeof(data));
      }
      if (dataLen < 0 || address < 0 || address > 255 || command < 0 || command > 255 || dataLen > (toWire(address) ? 64 : 128)) {
        request->send(400, "text/plain", "invalid packet"); return;
      }
      bool accepted = clunet.send(address, command, data, dataLen) != 0;
      request->send(accepted ? 200 : 503, "text/plain", accepted ? "accepted" : "bridge busy");
  });

  server.on("/discovery", HTTP_GET, [](AsyncWebServerRequest* request) {
    _request(request, CLUNET_ADDRESS_BROADCAST, CLUNET_COMMAND_DISCOVERY, NULL, 0, CLUNET_COMMAND_DISCOVERY_RESPONSE, DISCOVERY_RESPONSE_TIMEOUT_MS);
  });

  server.on("/request", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("c")){
      request->send(400, "text/plain", "/request?c=command_id[&a=device_address][&d=hex_data][&t=timeout_ms][&r=response_command_filter]");
      return;
    }
    int command = request->getParam("c")->value().toInt();
    int address = request->hasParam("a") ? request->getParam("a")->value().toInt() : CLUNET_ADDRESS_BROADCAST;
    int responseTimeout = request->hasParam("t") ? request->getParam("t")->value().toInt() : 100;
    int responseCommand = request->hasParam("r") ? request->getParam("r")->value().toInt() : -1;

    int dataLen = 0;
    char data[CLUNET_PACKET_DATA_SIZE];
    if (request->hasParam("d")){
      String hexData = request->getParam("d")->value();
      dataLen = hexStringToCharArray(data, hexData.c_str(), hexData.length(), sizeof(data));
    }
    if (dataLen < 0 || address < 0 || address > 255 || command < 0 || command > 255) {
      request->send(400, "text/plain", "invalid packet"); return;
    }
    _request(request, address, command, data, dataLen, responseCommand, responseTimeout);
  });

  server.on("/bridge/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(2048);
    doc["wifiStaConnected"] = WiFi.status() == WL_CONNECTED;
    doc["clunetReady"] = clunetConnected;
    doc["trafficMuted"] = FlashFirmware::isTrafficMuted();
    doc["udpPacketsSeen"] = clunet.udpPacketsSeen();
    doc["udpPacketsInvalidLength"] = clunet.udpPacketsInvalidLength();
    doc["udpPacketsInvalidSize"] = clunet.udpPacketsInvalidSize();
    doc["lastInvalidPacketLen"] = clunet.lastInvalidPacketLen();
    doc["lastInvalidPacketDeclaredSize"] = clunet.lastInvalidPacketDeclaredSize();
    doc["uartQueueLength"] = uartQueue.length();
    doc["multicastQueueLength"] = multicastQueue.length();
    doc["eventsQueued"] = eventsQueue.length();
    doc["apiRequestsQueued"] = apiRequestsQueue.length();
    doc["apiResponseActive"] = apiResponse != NULL;
    doc["activeDiscoveryRequestId"] = discoveryResponseActiveRequestId;
    doc["discoveryResponsesSniffed"] = discoveryResponsesSniffed;
    doc["discoveryResponsesMatchedActiveRequest"] = discoveryResponsesMatchedActiveRequest;
    doc["discoveryResponsesReturnedToHttp"] = discoveryResponsesReturnedToHttp;
    doc["discoveryResponseHttpCallbacks"] = discoveryResponseHttpCallbacks;
    doc["discoveryResponseLastSniffedAt"] = discoveryResponseLastSniffedAt;
    doc["discoveryResponseLastMatchedAt"] = discoveryResponseLastMatchedAt;
    doc["discoveryResponseLastReturnedAt"] = discoveryResponseLastReturnedAt;
    doc["discoveryResponseLastHttpCallbackAt"] = discoveryResponseLastHttpCallbackAt;
    doc["uartRxBuffered"] = uart_rx_data_len;
    doc["uartRxOverflow"] = uart_rx_overflow;
    doc["uartOverflows"] = uartOverflows;
    doc["uartInvalidFrames"] = invalidUart;
    doc["uartCrcErrors"] = uartCrcErrors;
    doc["uartFrameTimeouts"] = uartTimeouts;
    doc["uartDrops"] = uartDrops;
    doc["multicastDrops"] = multicastDrops;
    doc["eventDrops"] = eventDrops;
    const auto& link = BridgeTransport::diagnostics();
    doc["avrProtocolReady"] = link.protocol == 2 && millis() - link.lastReplyAt < 3000;
    doc["avrProtocol"] = link.protocol;
    doc["txBusy"] = BridgeTransport::busy();
    doc["txLastId"] = link.lastId; doc["txLastResult"] = link.lastResult;
    doc["txAccepted"] = link.accepted; doc["txTransmitted"] = link.transmitted;
    doc["txExpired"] = link.expired; doc["txRejected"] = link.rejected; doc["txUnknown"] = link.unknown;
    doc["avrUartHardwareErrors"] = link.avrHardwareErrors; doc["avrQueueDrops"] = link.avrQueueDrops; doc["avrUartOverflows"] = link.avrOverflows;
    doc["avrLegacyExpired"] = link.avrLegacyExpired; doc["avrCrcErrors"] = link.avrCrcErrors; doc["avrStackMinFree"] = link.avrStackMinFree;
    doc["eventsSuppressed"] = suppressedEvents; doc["eventsWithoutSubscribers"] = unobservedEvents;
    doc["uartHardwareOverruns"] = hardwareOverruns; doc["uartHardwareErrors"] = hardwareRxErrors;
    doc["eventSequence"] = event_id;
    doc["bootId"] = bootId;
    doc["uptimeMs"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["minHeap"] = minHeap;
    doc["maxFreeBlock"] = ESP.getMaxFreeBlockSize();
    doc["maxLoopMicros"] = maxLoopMicros;
    doc["resetReason"] = ESP.getResetReason();
    doc["reconnects"] = reconnects;

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });


  server.on("/api/switch", HTTP_GET, api_switch);
  
  server.on("/api/info/switch", HTTP_GET, info_switch);

  server.on("/api/dimmer", HTTP_GET, api_dimmer);

  server.on("/api/fan", HTTP_GET, api_fan);

  server.on("/api/info/fan", HTTP_GET, info_fan);

  server.on("/api/door", HTTP_GET, api_door);


  server.on("/registry/commands", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(4096);
    clunet_command cc;
    for (const auto c : CLUNET_COMMANDS){
      memcpy_P(&cc, &c, sizeof(clunet_command));
      doc[String(c.code)] = FPSTR(c.name);
    }

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });

  server.on("/registry/devices", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(2048);
    clunet_device cd;
    for (const auto d : CLUNET_DEVICES){
      memcpy_P(&cd, &d, sizeof(clunet_device));
      doc[String(d.code)] = FPSTR(d.name);
    }

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest*) {
    ESP.restart();
  });
  FlashFirmware::setupRoutes(server);

  events.authorizeConnect([](AsyncWebServerRequest*){ return events.count() < 4 && ESP.getFreeHeap() > 14000; });
  events.onConnect([](AsyncEventSourceClient *client){
     String state = String("{\"bootId\":") + bootId + ",\"sequence\":" + event_id + ",\"uptimeMs\":" + millis() + ",\"muted\":" + (FlashFirmware::isTrafficMuted() ? "true" : "false") + ",\"resync\":true}";
     client->send(state.c_str(), "RESET", event_id, 3000);
  });
  
  server.addHandler(&events);
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.serveStatic("/", LittleFS, "/www/").setDefaultFile("log.html");

  server.addHandler(new SPIFFSEditor("user", "111", LittleFS));

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404);
  });
  
  server.begin();
}

char check_crc(char* data, uint8_t size){
  uint8_t crc=0;
  for (uint8_t i=0; i<size; i++){
    uint8_t inbyte = data[i];
    for (uint8_t j=0; j<8; j++){
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix){
        crc ^= 0x8C;
      }
      inbyte >>= 1;
    }
  }
  return crc;
}

void on_uart_message(uint8_t code, char* data, uint8_t length){
  switch(code){
    case 4:
      BridgeTransport::receive(data, length);
      break;
    case UART_MESSAGE_CODE_CLUNET:
      if (clunet_packet::valid(data, length)){
        clunet_packet* packet = (clunet_packet*)data;
        FlashFirmware::observeApplicationPacket(packet);
        bool consumedByFlash = false;
        if (packet->command == CLUNET_COMMAND_BOOT_CONTROL){
          FlashFirmware::observeBootloaderResponse(packet);
          consumedByFlash = FlashFirmware::handleBootControlResponse(packet);
        }
        if (!CLUNET_MULTICAST_DEVICE(packet->src)){
           if (!consumedByFlash){
             clunet.ingest(packet, length);
             if (clunetConnected) queuePacket(multicastQueue, packet, multicastDrops);
           }
        }
      } else { ++invalidUart; }
      break;
    case UART_MESSAGE_CODE_FIRMWARE:
      break;
    case UART_MESSAGE_CODE_DEBUG:
      if (length) Serial1.println("debug message received: " + String((int8_t)data[0]));
      break;
  }
}

#define UART_RX_BUF_LENGTH 256
volatile char uart_rx_data[UART_RX_BUF_LENGTH];
volatile uint16_t uart_rx_data_len = 0;
bool uart_rx_overflow = false;

void analyze_uart_rx_trim(uint16_t offset){
  if (offset <= uart_rx_data_len){
    uart_rx_data_len -= offset;
    if (uart_rx_data_len){
      memmove((void*)uart_rx_data, (void*)(uart_rx_data + offset), uart_rx_data_len);
    }
  }
}

void analyze_uart_rx(void(*f)(uint8_t code, char* data, uint8_t length)){
  while (uart_rx_data_len > 1){
    //Serial1.println("len: " + uart_rx_data_len);
    uint16_t uart_rx_preambula_offset = uart_rx_data_len - 1;  //первый байт преамбулы может быть прочитан, а второй еще не пришел
    for (uint16_t i=0; i < uart_rx_data_len - 1; i++){
      if (uart_rx_data[i+0] == UART_MESSAGE_PREAMBULE[0] && uart_rx_data[i+1] == UART_MESSAGE_PREAMBULE[1]){
        uart_rx_preambula_offset = i;
        break;
      }
    }
    if (uart_rx_preambula_offset) {
      analyze_uart_rx_trim(uart_rx_preambula_offset ); //обрезаем мусор до преамбулы
    }

    if (uart_rx_data_len >= 5){ //минимальная длина сообщения с преамбулой
      char* uart_rx_message = (char*)(uart_rx_data + 2);
      uint8_t length = uart_rx_message[0];
      if (length < 3 || length > 79){
        //Serial1.println("invalid length");
        analyze_uart_rx_trim(2);  //пришел мусор, отрезаем преамбулу и надо пробовать искать преамбулу снова
        continue;
      }
          
      if (uart_rx_data_len >= length+2){    //в буфере данных уже столько, сколько описано в поле length
        if (check_crc(uart_rx_message, length - 1) == uart_rx_message[length - 1]){ //проверка crc
          //Serial1.println("crc ok");
          if (f){
            //Serial1.println("Uart message received: code: " + String((int)uart_rx_message[1]) + "; length: " + String(length - 3));
            f(uart_rx_message[1], &uart_rx_message[2], length - 3);
          }
          
          analyze_uart_rx_trim(length+2); //отрезаем прочитанное сообщение
          //Serial1.println("uart_rx_data_len: " + String(uart_rx_data_len));
        }else{
          ++uartCrcErrors;
          analyze_uart_rx_trim(2); 
        }
      }else{
        if (millis() - uartLastRxAt >= 100) { ++uartTimeouts; analyze_uart_rx_trim(2); continue; }
        break;
      }
    }else{
      if (millis() - uartLastRxAt >= 100) { ++uartTimeouts; analyze_uart_rx_trim(2); continue; }
      break;
    }
  }
}

void fillMessageData(JsonObject doc, clunet_packet* packet){
    if (packet->size){
      char hex[CLUNET_PACKET_DATA_SIZE * 2 + 1];
      if (charArrayToHexString(hex, packet->data, packet->size, sizeof(hex)) < 0) return;
      doc["hex"] = String(hex);

      fillObjData(doc.createNestedObject("obj"), packet);
    }
}

void fillObjData(JsonObject obj, clunet_packet* packet){
    alignas(float) char buf[512];
        
    switch(packet->command){
      case CLUNET_COMMAND_TEMPERATURE_INFO:{
        if (!getTemperatureInfo(packet->data, packet->size, buf, sizeof(buf))) { obj["decodeError"] = "invalid temperature payload"; break; }
        temperature_info* ti =(temperature_info*)buf;
        JsonArray sensors = obj.createNestedArray("sensors");
        for (int i=0; i<ti->num_sensors; i++){
          JsonObject sensor = sensors.createNestedObject();
          sensor["type"] = static_cast<unsigned char>(ti->sensors[i].type);
          sensor["id"] = ti->sensors[i].id;
          sensor["val"] = ti->sensors[i].value;
        }
      }
      break;
      case CLUNET_COMMAND_HUMIDITY_INFO:{
        if (!getHumidityInfo(packet->data, packet->size, buf, sizeof(buf))) { obj["decodeError"] = "invalid humidity payload"; break; }
        humidity_info* hi =(humidity_info*)buf;
        obj["val"] = hi->value;
      }
      break;
      case CLUNET_COMMAND_SWITCH_INFO:{
        if (packet->size != 1) { obj["decodeError"] = "invalid switch payload"; break; }
        JsonArray switches = obj.createNestedArray("switches");
        for (int i=0; i<8; i++){
          if (packet->data[0] & (1<<i)){
            JsonObject _switch = switches.createNestedObject();
            _switch["id"] = i+1;
            _switch["val"] = 1;
          }
        }
      }
      break;
    }
}

void fillValueData(JsonObject obj, clunet_packet* packet, char* cid){
    alignas(float) char buf[512];
    switch(packet->command){
      case CLUNET_COMMAND_TEMPERATURE_INFO:{
        if (!getTemperatureInfo(packet->data, packet->size, buf, sizeof(buf))) { obj["decodeError"] = "invalid temperature payload"; break; }
        temperature_info* ti =(temperature_info*)buf;
        for (int i=0; i<ti->num_sensors; i++){
          if (strcmp(ti->sensors[i].id, cid) == 0){
            obj["type"] = static_cast<unsigned char>(ti->sensors[i].type);
            obj["cid"] = ti->sensors[i].id;
            obj["val"] = ti->sensors[i].value;
            break;
          }
        }
      }
      break;
      case CLUNET_COMMAND_HUMIDITY_INFO:{
        if (!getHumidityInfo(packet->data, packet->size, buf, sizeof(buf))) { obj["decodeError"] = "invalid humidity payload"; break; }
        humidity_info* hi =(humidity_info*)buf;
        obj["val"] = hi->value;
      }
      break;
      case CLUNET_COMMAND_SWITCH_INFO: {
        if (packet->size != 1) { obj["decodeError"] = "invalid switch payload"; break; }
        int intId = String(cid).toInt();
        if (intId < 1 || intId > 8) break;
        obj["cid"] = cid;
        obj["value"] = (bool)(packet->data[0] & (1<<(intId-1)));
      }
      break;
      case CLUNET_COMMAND_FAN_INFO: {
        if (packet->size < 2) { obj["decodeError"] = "invalid fan payload"; break; }
        obj["mode"] = static_cast<unsigned char>(packet->data[0]);
        obj["value"] = (bool)(packet->data[1] == 3 || packet->data[1] == 4);
      }
      break;
    }
}

void fillMessageJsonObject(JsonObject doc, uint32_t timestamp_sec, uint16_t timestamp_ms, clunet_packet* packet){
    doc["c"] = packet->command;
    doc["s"] = packet->src;
    doc["d"] = packet->dst;

    if (timestamp_sec){
      char buf[4];
      snprintf(buf, sizeof(buf), "%03u", static_cast<unsigned>(timestamp_ms % 1000));
      doc["t"] = String(timestamp_sec) + String(buf);
    }

    if (packet->size){
      fillMessageData(doc.createNestedObject("m"), packet);
    }
}

void loop() {
  uint32_t loopStarted = micros();
  if (Serial.hasOverrun()) ++hardwareOverruns;
  if (Serial.hasRxError()) ++hardwareRxErrors;
  uint16_t readBudget = 256;
  while (readBudget-- && Serial.available() > 0) {
    if (uart_rx_data_len >= UART_RX_BUF_LENGTH) { ++uartOverflows; uart_rx_data_len = 0; }
    uart_rx_data[uart_rx_data_len++] = Serial.read();
    uartLastRxAt = millis();
    analyze_uart_rx(on_uart_message);
  }
  analyze_uart_rx(on_uart_message);
  BridgeTransport::process(FlashFirmware::legacyUartActive(), !uartQueue.isEmpty() || FlashFirmware::isTrafficMuted());
  BridgeTransport::Result tx = BridgeTransport::take(BridgeTransport::NORMAL);
  if (tx != BridgeTransport::NO_RESULT) {
    if (tx != BridgeTransport::TRANSMITTED) ++uartDrops;
    FlashFirmware::forwardTransportResult(tx);
  }
  FlashFirmware::process();
  if (apiResponse && (!getApiRequestWebRequest(apiResponse->state) || FlashFirmware::isTrafficMuted())) {
    AsyncWebServerRequest* request = getApiRequestWebRequest(apiResponse->state);
    if (request) request->send(503, "text/plain", "bridge maintenance");
    clunet.cancelRequest(); freeApiResponse();
  }

  static uint32_t nextNetworkAttempt = 0;
  static IPAddress multicastInterface;
  const uint32_t now = millis();
  if (clunetConnected && multicastInterface != WiFi.localIP()) {
    clunet.close(); clunetConnected = false; multicastQueue.clear();
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (clunetConnected) { clunet.close(); clunetConnected = false; multicastQueue.clear(); }
  } else if (!clunetConnected && static_cast<int32_t>(now - nextNetworkAttempt) >= 0) {
    nextNetworkAttempt = now + 2000;
    clunetConnected = clunet.connect();
    if (clunetConnected) { multicastInterface = WiFi.localIP(); ++reconnects; }
  }

  if (!uartQueue.isEmpty()) {
    clunet_packet* packet = uartQueue.front();
    uint32_t age = uartQueue.age(now);
    if (age >= uartQueue.budget() || (FlashFirmware::isTrafficMuted() && packet->command != CLUNET_COMMAND_BOOT_CONTROL)) {
      ++uartDrops; FlashFirmware::forwardTransportResult(BridgeTransport::EXPIRED); uartQueue.remove(packet);
    } else if (FlashFirmware::legacyUartActive() && packet->command == CLUNET_COMMAND_BOOT_CONTROL) {
      if (uart_send_message(packet)) uartQueue.remove(packet); // Only the unchanged UART bootloader of AVR itself.
    } else if (BridgeTransport::start(BridgeTransport::NORMAL, reinterpret_cast<char*>(packet), packet->len(), uartQueue.budget() - age)) {
      uartQueue.remove(packet);
    }
  }

  for (uint8_t i = 0; i < MULTICAST_MESSAGES_PER_LOOP && !multicastQueue.isEmpty(); ++i) {
    clunet_packet* packet = multicastQueue.front();
    if (multicastQueue.age(now) > 2000 || !clunetConnected) { ++multicastDrops; multicastQueue.remove(packet); continue; }
    if (!clunet.send_fake(packet->src, packet->dst, packet->command, packet->data, packet->size)) break;
    multicastQueue.remove(packet);
  }

  static uint32_t processedEventSequence = 0;
  if (FlashFirmware::isTrafficMuted() || !events.count()) {
    if (FlashFirmware::isTrafficMuted()) suppressedEvents += eventsQueue.length(); else unobservedEvents += eventsQueue.length();
    eventsQueue.clear(); processedEventSequence = event_id;
  }
  for (uint8_t i = 0; i < EVENT_MESSAGES_PER_LOOP && !eventsQueue.isEmpty(); ++i) {
    ts_clunet_packet* tp = eventsQueue.front();
    processedEventSequence = tp->sequence;
    if (eventsQueue.age(now) > 2000) { ++eventDrops; eventsQueue.remove(tp); continue; }
    JsonDocument doc;
    JsonObject row = doc.to<JsonArray>().add<JsonObject>();
    fillMessageJsonObject(row, tp->timestamp_sec, tp->timestamp_ms, tp->packet);
    row["seq"] = tp->sequence; row["bootId"] = bootId;
    row["uptimeMs"] = millis(); row["queuedMs"] = eventsQueue.age(now);
    if (!doc.overflowed()) {
      String json; serializeJson(doc, json);
      events.send(json.c_str(), "DATA", tp->sequence);
    } else ++eventDrops;
    eventsQueue.remove(tp);
  }
  if (eventsQueue.isEmpty()) processedEventSequence = event_id;
  static uint32_t lastHeartbeat = 0;
  if (now - lastHeartbeat >= 10000) {
    lastHeartbeat = now;
    String status = String("{\"bootId\":") + bootId + ",\"sequence\":" + processedEventSequence + ",\"muted\":" + (FlashFirmware::isTrafficMuted() ? "true}" : "false}");
    status.remove(status.length() - 1);
    const auto& link = BridgeTransport::diagnostics();
    status += String(",\"uptimeMs\":") + now + ",\"eventDrops\":" + eventDrops + ",\"suppressedEvents\":" + suppressedEvents + ",\"avrQueueDrops\":" + link.avrQueueDrops + ",\"avrUartErrors\":" + link.avrHardwareErrors + ",\"avrUartOverflows\":" + link.avrOverflows + ",\"avrCrcErrors\":" + link.avrCrcErrors + "}";
    events.send(status.c_str(), "SERVICE");
  }

  if (!FlashFirmware::isTrafficMuted() && apiResponse == NULL){
    while (!apiRequestsQueue.isEmpty()){
      api_request* ar = apiRequestsQueue.front();
      AsyncWebServerRequest* webRequest = getApiRequestWebRequest(ar->state);
      if (apiRequestsQueue.age(now) > 5000) {
        if (webRequest) webRequest->send(503, "text/plain", "request queue expired");
        apiRequestsQueue.remove(ar); continue;
      }
      if (webRequest != NULL){
        if (toWire(ar->address) && (!uartQueue.isEmpty() || BridgeTransport::busy())) break;
        apiResponse = (api_response*)malloc(sizeof(api_response));
        if (apiResponse == NULL){
          break;
        }
        apiResponse->state = retainApiRequestState(ar->state);
        apiResponse->info = ar->info;
        if (apiResponse->info){
          strcpy(apiResponse->infoId, ar->infoId);
        }
        apiResponse->responseFilterCommand = ar->responseFilterCommand;
        apiResponse->expectedPayload = -1;
        if (ar->command == CLUNET_COMMAND_HEATFLOOR && ar->size == 1) {
          uint8_t subtype = static_cast<uint8_t>(ar->data[0]);
          if (subtype == 0xFF) apiResponse->expectedPayload = -2;
          else if (subtype == 0xFE || (subtype >= 0xF0 && subtype <= 0xF9)) apiResponse->expectedPayload = subtype;
        }
        routingBudget = min(uint16_t(2000), uint16_t(ar->responseTimeout));
        apiResponse->requestId = clunet.request(ar->address, ar->command, ar->data, ar->size, [](clunet_packet* packet){
            bool matched = apiResponse != NULL && (apiResponse->responseFilterCommand < 0 || packet->command==apiResponse->responseFilterCommand);
            if (matched && apiResponse->expectedPayload != -1) {
              matched = packet->size > 0 && (apiResponse->expectedPayload == -2 ?
                static_cast<uint8_t>(packet->data[0]) <= 2 : static_cast<uint8_t>(packet->data[0]) == apiResponse->expectedPayload);
            }
            if (matched &&
                apiResponse != NULL &&
                apiResponse->requestId == (int)discoveryResponseActiveRequestId &&
                packet->command == CLUNET_COMMAND_DISCOVERY_RESPONSE){
              discoveryResponsesMatchedActiveRequest++;
              discoveryResponseLastMatchedAt = millis();
            }
            return matched;
        }, ar->responseTimeout);
        routingBudget = 2000;
        
        if (apiResponse->requestId){
          if (ar->address == CLUNET_ADDRESS_BROADCAST &&
              ar->command == CLUNET_COMMAND_DISCOVERY &&
              ar->responseFilterCommand == CLUNET_COMMAND_DISCOVERY_RESPONSE){
            discoveryResponseActiveRequestId = apiResponse->requestId;
          }else{
            discoveryResponseActiveRequestId = 0;
          }
          apiRequestsQueue.remove(ar);
        }else{
          freeApiResponse();
        }

        break;
      }else{
        apiRequestsQueue.remove(ar);
      }
    }
  }
  
  minHeap = min(minHeap, ESP.getFreeHeap());
  maxLoopMicros = max(maxLoopMicros, static_cast<uint32_t>(micros() - loopStarted));
  ArduinoOTA.handle();
  yield();
}
