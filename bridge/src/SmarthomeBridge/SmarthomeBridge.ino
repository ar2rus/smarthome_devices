/**
 * Use 3.0.2 esp8266 core
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
#include "Credentials.h"

#ifdef DEFAULT_MAX_SSE_CLIENTS
  #undef DEFAULT_MAX_SSE_CLIENTS 
  #define DEFAULT_MAX_SSE_CLIENTS 10
#endif

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 120);     //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsAddr(192, 168, 1, 1);

AsyncWebServer server(80);
AsyncEventSource events("/events");

ClunetMulticast clunet(CLUNET_ID, CLUNET_DEVICE);

long event_id = 0;
LinkedList<clunet_packet*> uartQueue = LinkedList<clunet_packet*>([](clunet_packet *m){ delete  m; });
LinkedList<clunet_packet*> multicastQueue = LinkedList<clunet_packet*>([](clunet_packet *m){ delete  m; });

LinkedList<ts_clunet_packet*> eventsQueue = LinkedList<ts_clunet_packet*>([](ts_clunet_packet *m){ delete  m; });

#define UART_MESSAGE_CODE_CLUNET 1
#define UART_MESSAGE_CODE_FIRMWARE 2
#define UART_MESSAGE_CODE_DEBUG 10

const char UART_MESSAGE_PREAMBULE[] = {0xC9, 0xE7};

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

AsyncWebServerRequest* request;
int responseCommand;

void _request(AsyncWebServerRequest* _request, uint8_t address, uint8_t command, char* data, uint8_t size,
                int responseFilterCommand, long responseTimeout){
//  if (request){
//    _request->send(423, "text/plain", "resource locked");
//    return;
//  }
  int requestId = clunet.request(address, command, data, size, 
    [](clunet_packet* packet){
      return responseCommand < 0 || packet->command==responseCommand;
    }, responseTimeout);
        
  if (!requestId){
    _request->send(423, "text/plain", "resource locked");
    return;
  }

  request = _request;
  responseCommand = responseFilterCommand;
}

void _response(int requestId, int numResponses){
  StaticJsonDocument<64> doc;
  doc["id"] = requestId;
  doc["num"] = numResponses;
  doc["memory"] = ESP.getFreeHeap();
  
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
  request = NULL;
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("\n\nHello");
  
  Serial.begin(38400, SERIAL_8N1);
  Serial.println("Booting");

  if (!LittleFS.begin()) {
    Serial1.println("LittleFS mount failed");
    return;
  }

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet, dnsAddr);

  pinMode(LED_BLUE_PORT, OUTPUT);  
  analogWrite(LED_BLUE_PORT, 12);
  
  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial1.println("Connection Failed! Rebooting...");
    delay(1000);
    ESP.restart();
  }
  Serial1.println("Connected");

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
  ArduinoOTA.begin();

  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  if (clunet.connect()){
    clunet.onPacketSniff([](clunet_packet* packet){
      
      timeval tv;
      gettimeofday(&tv, nullptr);
      
      if (CLUNET_MULTICAST_DEVICE(packet->src)){
          uartQueue.add(packet->copy());
      }

      ts_clunet_packet* tp = (ts_clunet_packet*)malloc(sizeof(ts_clunet_packet) + packet->len());
      packet->copy(&tp->packet);
      
      tp->timestamp_sec = (uint32_t)tv.tv_sec;
      tp->timestamp_ms = (uint16_t)(tv.tv_usec/1000UL);
      
      eventsQueue.add(tp);
    });
    
    clunet.onResponseReceived([](int requestId, LinkedList<clunet_response*>* responses){
      
      DynamicJsonDocument doc(4196);
      JsonObject root = doc.to<JsonObject>();
      root["id"] = requestId;
      root["memory"] = ESP.getFreeHeap();
      JsonArray docArray = root.createNestedArray("responses");
      
      for(auto i = responses->begin(); i != responses->end(); ++i){
        clunet_response* response = *i;
        if (requestId == response->requestId){
          fillMessageJsonObject(docArray.createNestedObject(), 0, 0, response->packet);
        }
      }
    
      if (request){
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
        request = NULL;
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
      char data[2 * CLUNET_PACKET_DATA_SIZE];
      if (request->hasParam("d")){
        String hexData = request->getParam("d")->value();
        dataLen = hexStringToCharArray(data, (char*)hexData.c_str(), hexData.length());
      }
      clunet.send(address, command, data, dataLen);
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

  events.onConnect([](AsyncEventSourceClient *client){
     client->send("Welcome", "SERVICE", 0, 3000);
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
    case UART_MESSAGE_CODE_CLUNET:
      if (data && length >= 4){
        clunet_packet* packet = (clunet_packet*)data;
        if (!CLUNET_MULTICAST_DEVICE(packet->src)){
           multicastQueue.add(packet->copy());
        }
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
      if (length < 3 || length > UART_RX_BUF_LENGTH){
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
    char buf[512];
    JsonObject obj = doc.createNestedObject("obj");
        
    switch(packet->command){
      case CLUNET_COMMAND_TEMPERATURE_INFO:{
        getTemperatureInfo(packet->data, buf);
        temperature_info* ti =(temperature_info*)buf;
        JsonArray sensors = obj.createNestedArray("sensors");
        for (int i=0; i<ti->num_sensors; i++){
          JsonObject sensor = sensors.createNestedObject();
          sensor["type"] = ti->sensors[i].type;
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
    
    int hexLen = 0;
    char hex[256];
    hexLen = charArrayToHexString(hex, packet->data, packet->size);
    doc["hex"] = String(hex);
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
  while (Serial.available() > 0 && uart_rx_data_len < UART_RX_BUF_LENGTH) {
    uart_rx_data[uart_rx_data_len++] = Serial.read();
  }
  analyze_uart_rx(on_uart_message);

  while (!uartQueue.isEmpty() && uart_can_send(uartQueue.front())){
    long nt = millis();
    if (nt - uart_time > DELAY_BETWEEN_UART_MESSAGES){
     uart_time = nt;
     clunet_packet* packet = uartQueue.front();
     uart_send_message(packet);
     uartQueue.remove(packet);
    }
  }

  while (!multicastQueue.isEmpty()){
    clunet_packet* packet = multicastQueue.front();
    clunet.send_fake(packet->src, packet->dst, packet->command, packet->data, packet->size);
    multicastQueue.remove(packet);
  }
  
  if (!eventsQueue.isEmpty() && events.avgPacketsWaiting()==0){
    DynamicJsonDocument doc(4196);
    while (!eventsQueue.isEmpty()){
      ts_clunet_packet* tp = eventsQueue.front();
      fillMessageJsonObject(doc.createNestedObject(), tp->timestamp_sec, tp->timestamp_ms, tp->packet);
      eventsQueue.remove(tp);
    }
    
    String json;
    serializeJson(doc, json);
    events.send(json.c_str(), "DATA", ++event_id);
  }
  
  ArduinoOTA.handle();
  yield();
}
