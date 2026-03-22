/**
 * Use 3.1.2 esp8266 core
 * lwip v2 Higher bandwidth; CPU 80 MHz
 * Flash size: 4M (FS: 1Mb / OTA: 1019 Kb) !!!
 * 
 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

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
#include "Config.h"

#ifdef DEFAULT_MAX_SSE_CLIENTS
  #undef DEFAULT_MAX_SSE_CLIENTS 
  #define DEFAULT_MAX_SSE_CLIENTS 10
#endif

AsyncWebServer server(80);
AsyncEventSource events("/events");

ClunetMulticast clunet(CLUNET_ID, CLUNET_DEVICE);

bool clunetReady = false;
bool clunetHandlersRegistered = false;
bool otaReady = false;
bool wifiStaConnectedPrev = false;
unsigned long clunetConnectAttemptAt = 0;
unsigned long clunetConnectedAt = 0;
unsigned long clunetLastConnectFailedAt = 0;

long event_id = 0;
#define EVENTS_QUEUE_MAX_LENGTH 128
#define EVENTS_HEARTBEAT_INTERVAL_MS 2000UL
#define DEVICE_LOG_DEDUP_WINDOW_MS 40
#define UART_QUEUE_MAX_LENGTH 64
#define MULTICAST_QUEUE_MAX_LENGTH 64
LinkedList<clunet_packet*> uartQueue = LinkedList<clunet_packet*>([](clunet_packet *m){ delete[] reinterpret_cast<char*>(m); });
LinkedList<clunet_packet*> multicastQueue = LinkedList<clunet_packet*>([](clunet_packet *m){ delete[] reinterpret_cast<char*>(m); });

LinkedList<ts_clunet_packet*> eventsQueue = LinkedList<ts_clunet_packet*>([](ts_clunet_packet *m){ free(m); });

LinkedList<api_request*> apiRequestsQueue = LinkedList<api_request*>([](api_request *r){ free(r); });
api_response* apiResponse = NULL;
unsigned long eventsHeartbeatAt = 0;
unsigned long uartPacketsReceivedCount = 0;
unsigned long multicastSniffPacketsCount = 0;
unsigned long multicastForwardedToUartCount = 0;
unsigned long multicastDroppedForMulticastDstCount = 0;
unsigned long multicastSentFromUartCount = 0;
unsigned long lastUartPacketAt = 0;
unsigned long lastMulticastPacketAt = 0;

#define UART_MESSAGE_CODE_CLUNET 1
#define UART_MESSAGE_CODE_FIRMWARE 2
#define UART_MESSAGE_CODE_DEBUG 10

const char UART_MESSAGE_PREAMBULE[] = {0xC9, 0xE7};
extern volatile unsigned char uart_rx_data_len;
extern bool uart_rx_overflow;
char check_crc(char* data, uint8_t size);

typedef struct {
  bool valid;
  uint8_t src;
  uint8_t dst;
  uint8_t command;
  uint8_t size;
  uint8_t dataCrc;
  unsigned long atMs;
} device_log_fingerprint;

device_log_fingerprint lastDeviceLog = {};

bool isDuplicateDeviceEvent(clunet_packet* packet){
  if (!packet){
    return false;
  }

  uint8_t dataCrc = packet->size ? static_cast<uint8_t>(check_crc(packet->data, packet->size)) : 0;
  unsigned long now = millis();
  bool duplicate = lastDeviceLog.valid &&
    lastDeviceLog.src == packet->src &&
    lastDeviceLog.dst == packet->dst &&
    lastDeviceLog.command == packet->command &&
    lastDeviceLog.size == packet->size &&
    lastDeviceLog.dataCrc == dataCrc &&
    (now - lastDeviceLog.atMs) <= DEVICE_LOG_DEDUP_WINDOW_MS;

  lastDeviceLog.valid = true;
  lastDeviceLog.src = packet->src;
  lastDeviceLog.dst = packet->dst;
  lastDeviceLog.command = packet->command;
  lastDeviceLog.size = packet->size;
  lastDeviceLog.dataCrc = dataCrc;
  lastDeviceLog.atMs = now;

  return duplicate;
}

void pushDeviceEvent(clunet_packet* packet){
  if (!packet || isDuplicateDeviceEvent(packet)){
    return;
  }

  timeval tv;
  gettimeofday(&tv, nullptr);

  ts_clunet_packet* tp = (ts_clunet_packet*)malloc(sizeof(ts_clunet_packet) + packet->len());
  if (!tp){
    return;
  }
  packet->copy(&tp->packet);

  tp->timestamp_sec = (uint32_t)tv.tv_sec;
  tp->timestamp_ms = (uint16_t)(tv.tv_usec / 1000UL);

  while (eventsQueue.length() >= EVENTS_QUEUE_MAX_LENGTH){
    eventsQueue.remove(eventsQueue.front());
  }
  eventsQueue.add(tp);
}

void enqueuePacket(LinkedList<clunet_packet*>& queue, clunet_packet* packet, uint16_t maxLength){
  if (!packet){
    return;
  }

  while (queue.length() >= maxLength){
    queue.remove(queue.front());
  }
  queue.add(packet);
}

bool shouldForwardSniffPacketToUart(clunet_packet* packet){
  if (!packet){
    return false;
  }
  if (packet->src == CLUNET_ID || !CLUNET_MULTICAST_DEVICE(packet->src)){
    return false;
  }
  if (packet->dst != CLUNET_ADDRESS_BROADCAST && CLUNET_MULTICAST_DEVICE(packet->dst)){
    return false;
  }
  return FlashFirmware::shouldForwardMulticastToUart(packet);
}

uint8_t buildTimeInfoPayload(char* out){
  time_t t = time(nullptr);
  if (!t){
    return 0;
  }

  struct tm timeinfo;
  localtime_r(&t, &timeinfo);
  if (timeinfo.tm_year <= 100){
    return 0;
  }

  out[0] = static_cast<char>(timeinfo.tm_year + 1900 - 2000);
  out[1] = static_cast<char>(timeinfo.tm_mon + 1);
  out[2] = static_cast<char>(timeinfo.tm_mday);
  out[3] = static_cast<char>(timeinfo.tm_hour);
  out[4] = static_cast<char>(timeinfo.tm_min);
  out[5] = static_cast<char>(timeinfo.tm_sec);
  out[6] = static_cast<char>(timeinfo.tm_wday == 0 ? 7 : timeinfo.tm_wday);
  return 7;
}

void pushLocalAutoResponseEvent(clunet_packet* request){
  if (!request || request->src == CLUNET_ID){
    return;
  }
  if (request->dst != CLUNET_ID && request->dst != CLUNET_ADDRESS_BROADCAST){
    return;
  }

  uint8_t responseCommand = 0;
  char payload[CLUNET_PACKET_DATA_SIZE];
  uint8_t payloadSize = 0;

  switch (request->command){
    case CLUNET_COMMAND_DISCOVERY:
      responseCommand = CLUNET_COMMAND_DISCOVERY_RESPONSE;
      payloadSize = static_cast<uint8_t>(strlen(CLUNET_DEVICE));
      if (payloadSize){
        memcpy(payload, CLUNET_DEVICE, payloadSize);
      }
      break;
    case CLUNET_COMMAND_PING:
      responseCommand = CLUNET_COMMAND_PING_REPLY;
      payloadSize = request->size;
      if (payloadSize){
        memcpy(payload, request->data, payloadSize);
      }
      break;
    case CLUNET_COMMAND_TIME:
      responseCommand = CLUNET_COMMAND_TIME_INFO;
      payloadSize = buildTimeInfoPayload(payload);
      break;
    default:
      return;
  }

  clunet_packet* packet = reinterpret_cast<clunet_packet*>(new char[sizeof(clunet_packet) + payloadSize]);
  if (!packet){
    return;
  }
  packet->src = CLUNET_ID;
  packet->dst = request->src;
  packet->command = responseCommand;
  packet->size = payloadSize;
  if (payloadSize){
    memcpy(packet->data, payload, payloadSize);
  }

  pushDeviceEvent(packet);
  delete[] reinterpret_cast<char*>(packet);
}

uint8_t uart_can_send(uint8_t length){
  return Serial.availableForWrite() >= length + 5;
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

void mirrorLocalPacketToUart(uint8_t address, uint8_t command, char* data, uint8_t size){
  clunet_packet* packet = reinterpret_cast<clunet_packet*>(new char[sizeof(clunet_packet) + size]);
  if (!packet){
    return;
  }

  packet->src = CLUNET_ID;
  packet->dst = address;
  packet->command = command;
  packet->size = size;
  if (size){
    memcpy(packet->data, data, size);
  }

  // Log bridge-origin commands from UI/API as regular events in the table.
  pushDeviceEvent(packet);

  if (CLUNET_MULTICAST_DEVICE(packet->src) && FlashFirmware::shouldForwardMulticastToUart(packet)){
    enqueuePacket(uartQueue, packet, UART_QUEUE_MAX_LENGTH);
  } else {
    delete[] reinterpret_cast<char*>(packet);
  }
}

size_t clunetSendMirrored(uint8_t address, uint8_t command, char* data, uint8_t size){
  size_t sent = clunet.send(address, command, data, size);
  if (sent){
    mirrorLocalPacketToUart(address, command, data, size);
  }
  return sent;
}


void _request(AsyncWebServerRequest* webRequest, uint8_t address, uint8_t command, char* data, uint8_t size,
                int responseFilterCommand, long responseTimeout, bool _infoRequest, String _infoRequestId){
    webRequest->client()->setRxTimeout(5);
    api_request* ar = (api_request*)malloc(sizeof(api_request) + size);
    if (!ar){
      webRequest->send(503, "text/plain", "busy");
      return;
    }
    ar->webRequest = webRequest;
    ar->info = _infoRequest;
    if (_infoRequest){
      strcpy(ar->infoId, _infoRequestId.c_str());
    }
    ar->address = address;
    ar->command = command;
    ar->responseFilterCommand = responseFilterCommand;
    ar->responseTimeout = responseTimeout;
    ar->size = size;
    memcpy(ar->data, data, size);
    apiRequestsQueue.add(ar);

    ar->webRequest->onDisconnect([&ar](){
      if (ar != NULL){
        ar->webRequest = NULL;
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
    AsyncResponseStream* response = request->beginResponseStream("text/plain");
    response->setCode(400);
    response->print(request->url());
    response->print("?");
    response->print(params);
    request->send(response);
}

void api_dimmer_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&id=channel_id&value=[0:100]");
}

void _api_dimmer(int address, int channel_id, int value){
    char data[] = {(char)channel_id, (char)map(value, 0, 100, 0, 255)};
    clunetSendMirrored(address, CLUNET_COMMAND_DIMMER, data, 2);
}

void api_dimmer(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("id") || !request->hasParam("value")){
        api_dimmer_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>100){
        api_dimmer_400(request);
        return;
    }

    _api_dimmer(address_param(request), id_param(request).toInt(), value);
    api_200(request); 
}

void api_switch_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&id=channel_id&value=[0:1]");
}

void _api_switch(int address, int channel_id, int value){
    char data[] = {(char)value, (char)channel_id};
    clunetSendMirrored(address, CLUNET_COMMAND_SWITCH, data, 2);
}

void api_switch(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("id") || !request->hasParam("value")){
        api_switch_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>1){
        api_switch_400(request);
        return;
    }

    _api_switch(address_param(request), id_param(request).toInt(), value);
    api_200(request);
}

void api_fan_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&value=[0:1]");
}

void _api_fan(int address, int value){
    char data = value ? 4 : 3;
    clunetSendMirrored(address, CLUNET_COMMAND_FAN, &data, 1);
}

void api_fan(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("value")){
        api_fan_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>1){
        api_fan_400(request);
        return;
    }

    _api_fan(address_param(request), value);
    api_200(request);
}

void api_door_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&value=[0:1]");
}

void _api_door(int address, int value){
    clunetSendMirrored(address, CLUNET_COMMAND_DOOR, (char*)&value, 1);
}

void api_door(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("value")){
        api_door_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>1){
        api_fan_400(request);
        return;
    }

    _api_door(address_param(request), value);
    api_200(request);
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

void setupClunetCallbacks(){
  if (clunetHandlersRegistered){
    return;
  }

  clunet.onPacketSniff([](clunet_packet* packet){
    multicastSniffPacketsCount++;
    lastMulticastPacketAt = millis();

    if (packet->src != CLUNET_ID &&
        CLUNET_MULTICAST_DEVICE(packet->src) &&
        packet->dst != CLUNET_ADDRESS_BROADCAST &&
        CLUNET_MULTICAST_DEVICE(packet->dst)){
      multicastDroppedForMulticastDstCount++;
    }

    if (shouldForwardSniffPacketToUart(packet)){
      enqueuePacket(uartQueue, packet->copy(), UART_QUEUE_MAX_LENGTH);
      multicastForwardedToUartCount++;
    }

    pushDeviceEvent(packet);
    pushLocalAutoResponseEvent(packet);
  });

  clunet.onResponseReceived([](int requestId, LinkedList<clunet_response*>* responses){
    if (apiResponse != NULL /**&& apiResponse->requestId == requestId**/){
      if (apiResponse->webRequest != NULL){
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
            if (apiResponse->info){
              fillValueData(root, response->packet, apiResponse->infoId);
              break;
            } else {
              fillMessageJsonObject(docArray.createNestedObject(), 0, 0, response->packet);
            }
          }
        }

        String json;
        serializeJson(doc, json);
        apiResponse->webRequest->send(200, "application/json", json);
      }
      free(apiResponse);
      apiResponse = NULL;
    }
  });

  clunetHandlersRegistered = true;
}

void ensureClunetConnected(){
  if (!BridgeConfig::isStaConnected() || clunetReady){
    return;
  }

  unsigned long now = millis();
  if (clunetConnectAttemptAt && (now - clunetConnectAttemptAt) < CLUNET_CONNECT_RETRY_INTERVAL_MS){
    return;
  }
  clunetConnectAttemptAt = now;

  if (clunet.connect()){
    setupClunetCallbacks();
    clunetReady = true;
    clunetConnectedAt = millis();
    Serial1.println("Clunet connected");
  } else {
    clunetLastConnectFailedAt = millis();
  }
}

void sendConfigPage(AsyncWebServerRequest* request){
  BridgeConfig::sendPage(request);
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("\n\nHello");
  
  Serial.begin(38400, SERIAL_8N1);
  Serial.println("Booting");
  FlashFirmware::init();

  pinMode(LED_BLUE_PORT, OUTPUT);
  analogWrite(LED_BLUE_PORT, 12);

  if (!LittleFS.begin()) {
    Serial1.println("LittleFS mount failed");
    return;
  }

  Serial.swap();
  Serial.flush();
  Serial.setRxBufferSize(256);  //as default

  ArduinoOTA.setHostname("smarthome-bridge");
  ArduinoOTA.onStart([]() {
    Serial1.println("ArduinoOTA start update");
    if (ArduinoOTA.getCommand() == U_FS) {
      LittleFS.end();
    }
  });
  BridgeConfig::init();

  server.on("/command", HTTP_GET, [](AsyncWebServerRequest* request) {
       if (!request->hasParam("c")){
        request->send(400, "text/plain", "/command?c=command_id[&a=device_address][&d=hex_data]");
        return;
      }
      int command = request->getParam("c")->value().toInt();
      int address = request->hasParam("a") ? request->getParam("a")->value().toInt() : CLUNET_ADDRESS_BROADCAST;

      int dataLen = 0;
      char data[2 * CLUNET_PACKET_DATA_SIZE];
      if (request->hasParam("d")){
        String hexData = request->getParam("d")->value();
        dataLen = hexStringToCharArray(data, (char*)hexData.c_str(), hexData.length());
      }
      clunetSendMirrored(address, command, data, dataLen);
      request->send(200, "text/plain", "OK");
  });

  server.on("/discovery", HTTP_GET, [](AsyncWebServerRequest* request) {
    _request(request, CLUNET_ADDRESS_BROADCAST, CLUNET_COMMAND_DISCOVERY, NULL, 0, CLUNET_COMMAND_DISCOVERY_RESPONSE, 500);
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
    char data[2 * CLUNET_PACKET_DATA_SIZE];
    if (request->hasParam("d")){
      String hexData = request->getParam("d")->value();
      dataLen = hexStringToCharArray(data, (char*)hexData.c_str(), hexData.length());
    }
    _request(request, address, command, data, dataLen, responseCommand, responseTimeout);
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

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) {
    ESP.restart();
  });

  server.on("/bridge/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->print(F("{\"wifiStaConnected\":"));
    response->print(BridgeConfig::isStaConnected() ? F("true") : F("false"));
    response->print(F(",\"clunetReady\":"));
    response->print(clunetReady ? F("true") : F("false"));
    response->print(F(",\"clunetHandlersRegistered\":"));
    response->print(clunetHandlersRegistered ? F("true") : F("false"));
    response->print(F(",\"trafficMuted\":"));
    response->print(FlashFirmware::isTrafficMuted() ? F("true") : F("false"));
    response->print(F(",\"clunetConnectAttemptAt\":"));
    response->print(clunetConnectAttemptAt);
    response->print(F(",\"clunetConnectedAt\":"));
    response->print(clunetConnectedAt);
    response->print(F(",\"clunetLastConnectFailedAt\":"));
    response->print(clunetLastConnectFailedAt);
    response->print(F(",\"uartPacketsReceived\":"));
    response->print(uartPacketsReceivedCount);
    response->print(F(",\"multicastSniffPackets\":"));
    response->print(multicastSniffPacketsCount);
    response->print(F(",\"multicastForwardedToUart\":"));
    response->print(multicastForwardedToUartCount);
    response->print(F(",\"multicastDroppedForMulticastDst\":"));
    response->print(multicastDroppedForMulticastDstCount);
    response->print(F(",\"multicastSentFromUart\":"));
    response->print(multicastSentFromUartCount);
    response->print(F(",\"lastUartPacketAt\":"));
    response->print(lastUartPacketAt);
    response->print(F(",\"lastMulticastPacketAt\":"));
    response->print(lastMulticastPacketAt);
    response->print(F(",\"uartQueueLength\":"));
    response->print(uartQueue.length());
    response->print(F(",\"multicastQueueLength\":"));
    response->print(multicastQueue.length());
    response->print(F(",\"uartRxBuffered\":"));
    response->print(uart_rx_data_len);
    response->print(F(",\"uartRxOverflow\":"));
    response->print(uart_rx_overflow ? F("true") : F("false"));
    response->print(F(",\"eventsQueued\":"));
    response->print(eventsQueue.length());
    response->print(F(",\"apiRequestQueued\":"));
    response->print(apiRequestsQueue.length());
    response->print(F(",\"apiResponseActive\":"));
    response->print(apiResponse != NULL ? F("true") : F("false"));
    response->print(F("}"));
    request->send(response);
  });

  BridgeConfig::setupRoutes(server);
  FlashFirmware::setupRoutes(server);

  server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request){
    request->send(LittleFS, "/www/log.html", "text/html");
  });

  server.on("/log.html", HTTP_GET, [](AsyncWebServerRequest* request){
    request->redirect("/log");
  });

  server.on("/flash", HTTP_GET, [](AsyncWebServerRequest* request){
    request->send(LittleFS, "/www/flash.html", "text/html");
  });

  server.on("/flash.html", HTTP_GET, [](AsyncWebServerRequest* request){
    request->redirect("/flash");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request){
    if (BridgeConfig::isApMode()){
      sendConfigPage(request);
    } else {
      request->send(LittleFS, "/www/log.html", "text/html");
    }
  });

  events.onConnect([](AsyncEventSourceClient *client){
     client->send("Welcome", "SERVICE", 0, 3000);
  });
  
  server.addHandler(&events);
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.serveStatic("/", LittleFS, "/www/");

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
    case UART_MESSAGE_CODE_CLUNET:
      if (data && length >= 4){
        uartPacketsReceivedCount++;
        lastUartPacketAt = millis();
        clunet_packet* packet = (clunet_packet*)data;
        bool consumedByFlash = false;
        if (packet->command == CLUNET_COMMAND_BOOT_CONTROL){
          FlashFirmware::touchBootloaderActivity(packet->src);
          consumedByFlash = FlashFirmware::handleBootControlResponse(packet);
        }
        if (!CLUNET_MULTICAST_DEVICE(packet->src)){
           if (!consumedByFlash){
             if (clunetReady && BridgeConfig::isStaConnected()){
               enqueuePacket(multicastQueue, packet->copy(), MULTICAST_QUEUE_MAX_LENGTH);
             }
           }
        }
        pushDeviceEvent(packet);
        pushLocalAutoResponseEvent(packet);
      }
      break;
    case UART_MESSAGE_CODE_FIRMWARE:
      if (data && length >= 4){
        clunet_packet* packet = (clunet_packet*)data;
      }
      break;
    case UART_MESSAGE_CODE_DEBUG:
      Serial1.println("debug message received: " + String((int8_t)data[0]));
      break;
  }
}

#define UART_RX_BUF_LENGTH 256
volatile char uart_rx_data[UART_RX_BUF_LENGTH];
volatile unsigned char uart_rx_data_len = 0;
bool uart_rx_overflow = false;

void analyze_uart_rx_trim(uint8_t offset){
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
    uint8_t uart_rx_preambula_offset = uart_rx_data_len - 1;  //первый байт преамбулы может быть прочитан, а второй еще не пришел
    for (uint8_t i=0; i < uart_rx_data_len - 1; i++){
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
      if (length < 3 || length > (UART_RX_BUF_LENGTH - 2)){
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
          //Serial1.println("crc error");
          analyze_uart_rx_trim(2); 
        }
      }else{
        //Serial1.println("not enough data: " + String(uart_rx_data_len));
        break;
      }
    }else{
      //Serial1.println("too short message yet (length<5)");
      break;
    }
  }
}

void fillMessageData(JsonObject doc, clunet_packet* packet){
    if (packet->size){
      int hexLen = 0;
      char hex[256];
      hexLen = charArrayToHexString(hex, packet->data, packet->size);
      doc["hex"] = String(hex);

      fillObjData(doc.createNestedObject("obj"), packet);
    }
}

void fillObjData(JsonObject obj, clunet_packet* packet){
    char buf[512];
        
    switch(packet->command){
      case CLUNET_COMMAND_TEMPERATURE_INFO:{
        getTemperatureInfo(packet->data, buf);
        temperature_info* ti =(temperature_info*)buf;
        JsonArray sensors = obj.createNestedArray("sensors");
        for (int i=0; i<ti->num_sensors; i++){
          JsonObject sensor = sensors.createNestedObject();
          sensor["type"] = static_cast<unsigned char>(ti->sensors[i].type);
          sensor["id"] = ti->sensors[i].id;
          sensor["val"] = serialized(String(ti->sensors[i].value, 2));
        }
      }
      break;
      case CLUNET_COMMAND_HUMIDITY_INFO:{
        getHumidityInfo(packet->data, buf);
        humidity_info* hi =(humidity_info*)buf;
        obj["val"] = serialized(String(hi->value, 2)); 
      }
      break;
      case CLUNET_COMMAND_SWITCH_INFO:{
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
    char buf[512];
    switch(packet->command){
      case CLUNET_COMMAND_TEMPERATURE_INFO:{
        getTemperatureInfo(packet->data, buf);
        temperature_info* ti =(temperature_info*)buf;
        for (int i=0; i<ti->num_sensors; i++){
          if (strcmp(ti->sensors[i].id, cid)){
            obj["type"] = static_cast<unsigned char>(ti->sensors[i].type);
            obj["cid"] = ti->sensors[i].id;
            obj["val"] = serialized(String(ti->sensors[i].value, 2));  
            break;
          }
        }
      }
      break;
      case CLUNET_COMMAND_HUMIDITY_INFO:{
        getHumidityInfo(packet->data, buf);
        humidity_info* hi =(humidity_info*)buf;
        obj["val"] = serialized(String(hi->value, 2)); 
      }
      break;
      case CLUNET_COMMAND_SWITCH_INFO: {
        int intId = String(cid).toInt();
        obj["cid"] = cid;
        obj["value"] = (bool)(packet->data[0] & (1<<(intId-1)));
      }
      break;
      case CLUNET_COMMAND_FAN_INFO: {
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
      sprintf(buf, "%03d", timestamp_ms);
      doc["t"] = String(timestamp_sec) + String(buf);
    }

    if (packet->size){
      fillMessageData(doc.createNestedObject("m"), packet);
    }
}

#define DELAY_BETWEEN_UART_MESSAGES 5
long uart_time = 0;

void loop() {
  while (Serial.available() > 0) {
    char byte = Serial.read();
    if (uart_rx_data_len < UART_RX_BUF_LENGTH) {
      uart_rx_data[uart_rx_data_len++] = byte;
    } else {
      uart_rx_overflow = true;
    }
  }

  if (uart_rx_overflow) {
    uart_rx_data_len = 0;
    uart_rx_overflow = false;
  }

  analyze_uart_rx(on_uart_message);
  FlashFirmware::process();
  BridgeConfig::loop();

  bool wifiStaConnected = BridgeConfig::isStaConnected();
  if (wifiStaConnected && !wifiStaConnectedPrev){
    const char* timezone = BridgeConfig::timezone();
    if (timezone && timezone[0]){
      configTime(timezone, "pool.ntp.org", "time.nist.gov");
    }
    ArduinoOTA.begin();
    otaReady = true;
    clunetReady = false;
    clunetConnectAttemptAt = millis() - CLUNET_CONNECT_RETRY_INTERVAL_MS;
    Serial1.print("WiFi connected: ");
    Serial1.println(WiFi.localIP());
  } else if (!wifiStaConnected && wifiStaConnectedPrev){
    otaReady = false;
    clunetReady = false;
  }
  wifiStaConnectedPrev = wifiStaConnected;

  if (wifiStaConnected){
    ensureClunetConnected();
  }

  while (!uartQueue.isEmpty() && uart_can_send(uartQueue.front())){
    long nt = millis();
    if (nt - uart_time > DELAY_BETWEEN_UART_MESSAGES){
     uart_time = nt;
     clunet_packet* packet = uartQueue.front();
     if (uart_send_message(packet)){
       uartQueue.remove(packet);
     }else{
       break;
     }
    }
  }

  while (!multicastQueue.isEmpty()){
    clunet_packet* packet = multicastQueue.front();
    if (clunet.send_fake(packet->src, packet->dst, packet->command, packet->data, packet->size)){
      multicastSentFromUartCount++;
      multicastQueue.remove(packet);
    }else{
      break;
    }
  }
  
  if (!eventsQueue.isEmpty() && events.avgPacketsWaiting()==0){
    DynamicJsonDocument doc(4196);
    JsonArray batch = doc.to<JsonArray>();
    while (!eventsQueue.isEmpty()){
      ts_clunet_packet* tp = eventsQueue.front();
      fillMessageJsonObject(batch.createNestedObject(), tp->timestamp_sec, tp->timestamp_ms, tp->packet);
      eventsQueue.remove(tp);
    }
    
    String json;
    serializeJson(doc, json);
    events.send(json.c_str(), "DATA", ++event_id);
  }

  if ((millis() - eventsHeartbeatAt) >= EVENTS_HEARTBEAT_INTERVAL_MS){
    eventsHeartbeatAt = millis();
    events.send("ping", "SERVICE", ++event_id);
  }

  if (!FlashFirmware::isTrafficMuted() && apiResponse == NULL){
    while (!apiRequestsQueue.isEmpty()){
      api_request* ar = apiRequestsQueue.front();
      if (ar->webRequest != NULL){
        
        apiResponse = (api_response*)malloc(sizeof(api_response));
        if (apiResponse == NULL){
          break;
        }
        apiResponse->webRequest = ar->webRequest;
        apiResponse->info = ar->info;
        if (apiResponse->info){
          strcpy(apiResponse->infoId, ar->infoId);
        }
        apiResponse->responseFilterCommand = ar->responseFilterCommand;
        apiResponse->requestId = clunet.request(ar->address, ar->command, ar->data, ar->size, [](clunet_packet* packet){
            return apiResponse->responseFilterCommand < 0 || packet->command==apiResponse->responseFilterCommand;
        }, ar->responseTimeout);
        
        if (apiResponse->requestId){
          mirrorLocalPacketToUart(ar->address, ar->command, ar->data, ar->size);
          apiResponse->webRequest->onDisconnect([](){
            if (apiResponse != NULL){
              apiResponse->webRequest = NULL;
            }
          });
       
          apiRequestsQueue.remove(ar);
        }else{
          free(apiResponse);
          apiResponse = NULL;   
        }

        break;
      }else{
        apiRequestsQueue.remove(ar);
      }
    }
  }
  
  if (otaReady){
    ArduinoOTA.handle();
  }
  yield();
}
