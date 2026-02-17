/**
    Use 3.1.2 esp8266 core
    lwip v2 Higher bandwidth; CPU 80 MHz
    4M (FS: 1Mb OTA:~1019Kb)

    dependencies:
    https://github.com/me-no-dev/ESPAsyncWebServer
    https://github.com/ar2rus/ClunetMulticast

 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include <ESP8266HTTPClient.h>

#include "HeatingController.h"
#include "Credentials.h"

#include <ArduinoJson.h>

#include <time.h>
#include <LittleFS.h>  // Используем LittleFS


OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature DS18B20(&oneWire);

float DS18B20_values[ONE_WIRE_NUM_DEVICES];


bool relay_states[RELAY_NUM];
unsigned long relay_state_changed_at[RELAY_NUM];

static const char* ROOM_SENSOR_IDS[RELAY_NUM] = {
  "28B82156B5013C0C",
  "28616434294EF5B4",
  "286164342BFE6D2A",
  "28616434280D4260"
};

static const char* ROOM_TEMP_ENDPOINT = "http://192.168.50.129/api/temperatures";
static float cached_room_temps[RELAY_NUM];
static unsigned long last_room_fetch_ms = 0;
static bool has_room_cache = false;

bool fetchRoomTemperatures(float outTemps[RELAY_NUM]) {
  for (int i=0; i<RELAY_NUM; i++){
    outTemps[i] = NAN;
  }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(800);
  if (!http.begin(client, ROOM_TEMP_ENDPOINT)) {
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(1536);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    return false;
  }

  JsonArray sensors = doc["sensors"].as<JsonArray>();
  if (sensors.isNull()) {
    return false;
  }

  for (JsonObject sensor : sensors) {
    String id = sensor["id"] | "";
    if (id.length() == 0) {
      continue;
    }
    id.toUpperCase();

    float temperature = NAN;
    JsonVariant tempVar = sensor["temperature"];
    if (!tempVar.isNull()) {
      temperature = tempVar.as<float>();
    }

    for (int i=0; i<RELAY_NUM; i++){
      if (id.equals(ROOM_SENSOR_IDS[i])) {
        outTemps[i] = temperature;
      }
    }
  }

  return true;
}

void ensureRoomTemperatureCache() {
  unsigned long now = millis();
  if (has_room_cache && (now - last_room_fetch_ms) < 10000UL) {
    return;
  }

  float temps[RELAY_NUM];
  bool ok = fetchRoomTemperatures(temps);
  last_room_fetch_ms = now;
  if (ok) {
    for (int i=0; i<RELAY_NUM; i++){
      cached_room_temps[i] = temps[i];
    }
    has_room_cache = true;
  } else if (!has_room_cache) {
    for (int i=0; i<RELAY_NUM; i++){
      cached_room_temps[i] = NAN;
    }
  }
}

void relay_state(int index, bool _on){
  if (relay_states[index] != _on){
     relay_states[index] = _on;
     relay_state_changed_at[index] = millis();
     apply_relay_state(index);
  }
}

void apply_relay_state(int index){
  digitalWrite(RELAY_PIN[index], relay_states[index] ? HIGH : LOW);
}

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 50, 127); //Node static IP
IPAddress gateway(192, 168, 50, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsAddr(192, 168, 50, 1);

AsyncWebServer server(80); // Порт 80
ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);


void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  // Инициализируем выходы для реле
  for (int i=0; i<RELAY_NUM; i++){
    relay_states[i] = false;
    relay_state_changed_at[i] = millis();
    pinMode(RELAY_PIN[i], OUTPUT);
    apply_relay_state(i);
  }

  // Инициализируем файловую систему LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
  }
  

  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet, dnsAddr);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(1000);
    ESP.restart();
  }

  ArduinoOTA.setHostname("heating-controller");
  
  ArduinoOTA.onStart([]() {
    Serial.println("ArduinoOTA start update");
  });

  ArduinoOTA.begin();

  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  
  if (clunet.connect()){
    clunet.onPacketReceived([](clunet_packet* packet){
//       switch (packet->command) {
//        case CLUNET_COMMAND_FAN: {
//          if (packet->size == 0) {
//            fanControllers[FAN_BATHROOM_CHANNEL]->toggle();
//          }else if (packet->size == 1){
//            if (packet->data[0] == 0x00) {
//               fanControllers[FAN_BATHROOM_CHANNEL]->turnOff();
//            } else if (packet->data[0] == 0x01) {
//              fanControllers[FAN_BATHROOM_CHANNEL]->turnOn();
//            } else if (packet->data[0] == 0x02) {
//              fanControllers[FAN_BATHROOM_CHANNEL]->turnOnWithTimer();
//            }
//          }
//        
//        break;
//        }
//       }
    });
  }

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });


  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    ESP.restart();
  });

  server.on("/api/relay/state", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(768);
    JsonArray channels = doc.createNestedArray("channels");
    unsigned long now = millis();
    const int channelMap[RELAY_NUM] = {
      DS18B20_CHANNEL_1,
      DS18B20_CHANNEL_2,
      DS18B20_CHANNEL_3,
      DS18B20_CHANNEL_4
    };
    if (DS18B20_INPUT >= 0 && DS18B20_INPUT < ONE_WIRE_NUM_DEVICES && isValidT(DS18B20_values[DS18B20_INPUT])) {
      doc["inputTemp"] = DS18B20_values[DS18B20_INPUT];
    } else {
      doc["inputTemp"] = nullptr;
    }

    for (int i=0; i<RELAY_NUM; i++){
      JsonObject channel = channels.createNestedObject();
      channel["index"] = i;
      channel["on"] = relay_states[i];
      unsigned long elapsed = now - relay_state_changed_at[i];
      channel["elapsedMs"] = elapsed;
      int tempIndex = channelMap[i];
      if (tempIndex >= 0 && tempIndex < ONE_WIRE_NUM_DEVICES && isValidT(DS18B20_values[tempIndex])) {
        channel["returnTemp"] = DS18B20_values[tempIndex];
      } else {
        channel["returnTemp"] = nullptr;
      }
      if (isValidT(cached_room_temps[i])) {
        channel["roomTemp"] = cached_room_temps[i];
      } else {
        channel["roomTemp"] = nullptr;
      }
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/relay/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasArg("index")){
      request->send(400, "text/plain", "Missing index\n");
      return;
    }
    int i = request->arg("index").toInt();
    if (i < 0 || i >= RELAY_NUM){
      request->send(400, "text/plain", "Invalid index\n");
      return;
    }

    relay_state(i, !relay_states[i]);

    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["index"] = i;
    doc["on"] = relay_states[i];
    doc["elapsedMs"] = millis() - relay_state_changed_at[i];
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/onewire/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    JsonArray devices = doc.createNestedArray("devices");

    uint8_t addr[8];
    oneWire.reset_search();
    while (oneWire.search(addr)) {
      if (OneWire::crc8(addr, 7) != addr[7]) {
        continue;
      }

      String id;
      for (int i=0; i<8; i++){
        if (addr[i] < 16) id += "0";
        id += String(addr[i], HEX);
      }
      id.toUpperCase();
      devices.add(id);
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/onewire/values", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    JsonObject values = doc.createNestedObject("values");
    for (int i=0; i<ONE_WIRE_NUM_DEVICES; i++){
      String id;
      for (int b=0; b<8; b++){
        if (DS18B20_DEVICES[i][b] < 16) id += "0";
        id += String(DS18B20_DEVICES[i][b], HEX);
      }
      id.toUpperCase();

      if (isValidT(DS18B20_values[i])){
        values[id] = DS18B20_values[i];
      } else {
        values[id] = nullptr;
      }
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  
  // Обработчик для корневого маршрута - отдаем index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  // Обработчик для загрузки статических файлов (CSS, JS, изображения)
  server.serveStatic("/", LittleFS, "/");
  
  server.onNotFound( [](AsyncWebServerRequest *request) {
    server_response(request, 404);
  });

  server.begin();

  for (int i=0; i<ONE_WIRE_NUM_DEVICES; i++){
    DS18B20_values[i] = DEVICE_DISCONNECTED_C;
  }
  DS18B20.begin();
  DS18B20.setResolution(10);

}

bool isValidT(float t){
  return t>-55 && t<125;
}

void server_response(AsyncWebServerRequest *request, unsigned int response) {
  switch (response) {
    case 200:
      request->send(200);
      break;
    case 400:
      request->send(400, "text/plain", "Bad request\n\n");
      break;
    default:
      //case 404:
      request->send(404, "text/plain", "File Not Found\n\n");
      break;
  }
}

int prev_n = -1;

void loop() {
  long m = millis();
  ensureRoomTemperatureCache();
  int p = m % DS18B20_REQUEST_PERIOD;
  if (p == 0){
    int n = (m / DS18B20_REQUEST_PERIOD) % DS18B20_NUM_REQUESTS;
    if (n != prev_n){
      prev_n = n;
      if (n == 0){
        DS18B20.requestTemperatures();
      }else{
        float t = DS18B20.getTempC(DS18B20_DEVICES[n-1]);
        if (isValidT(t)){
          DS18B20_values[n-1] = t;
        } else {
          DS18B20_values[n-1] = DEVICE_DISCONNECTED_C;
        }
      }
    }
  }
  
  ArduinoOTA.handle();
  yield();
}
