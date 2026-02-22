/**
    Use 3.1.2 esp8266 core
    lwip v2 Higher bandwidth; CPU 80 MHz
    4M (FS: 1Mb OTA:~1019Kb)

    dependencies:
    https://github.com/me-no-dev/ESPAsyncWebServer
    AsyncMqttClient

 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>

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

struct DS18B20Reading {
  float temperature;
  time_t timestamp;

  bool hasValue() const {
    return timestamp > 0;
  }

  bool hasActualValue() const {
    static const time_t ACTUAL_VALUE_MAX_AGE_SEC = 30 * 60;
    if (!hasValue()) {
      return false;
    }
    time_t now = time(nullptr);
    return now >= timestamp && (now - timestamp) <= ACTUAL_VALUE_MAX_AGE_SEC;
  }
};

DS18B20Reading DS18B20_values[ONE_WIRE_NUM_DEVICES];


bool relay_states[RELAY_NUM];
unsigned long relay_state_changed_at[RELAY_NUM];

static const char* ROOM_SENSOR_IDS[RELAY_NUM] = {
  "28B82156B5013C0C",
  "28616434294EF5B4",
  "286164342BFE6D2A",
  "28616434280D4260"
};

static float cached_room_temps[RELAY_NUM];
static unsigned long last_room_fetch_ms = 0;
static bool has_room_cache = false;
static const unsigned long MQTT_RECONNECT_PERIOD_MS = 5000UL;
unsigned long lastMqttReconnect = 0;
bool mqttWasConnected = false;

AsyncMqttClient mqttClient;
void publishMqttAllSensorsMeta();
void publishMqttAllSensorsState();

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!mqttClient.connected()) {
    mqttClient.connect();
  }
}

void publishMqttStartMessages() {
  mqttClient.publish(MQTT_TOPIC_STATUS, 1, true, "online");
  publishMqttAllSensorsMeta();
  publishMqttAllSensorsState();
}

String formatDeviceId(const uint8_t* address) {
  char buffer[17];
  snprintf(
    buffer,
    sizeof(buffer),
    "%02X%02X%02X%02X%02X%02X%02X%02X",
    address[0], address[1], address[2], address[3],
    address[4], address[5], address[6], address[7]
  );
  return String(buffer);
}

String mqttSensorMetaPayload(int sensorIndex) {
  String payload = "{";
  payload += "\"type\":\"DS18B20\",";
  payload += "\"units\":{";
  payload += "\"temperature\":\"C\"";
  payload += "},";
  payload += "\"location\":\"" + String(DS18B20_DEVICES_LOCATIONS[sensorIndex]) + "\"";
  payload += "}";
  return payload;
}

String mqttSensorStatePayload(int sensorIndex) {
  String payload = "{";
  payload += "\"temperature\":" + String(DS18B20_values[sensorIndex].temperature, 2) + ",";
  payload += "\"timestamp\":" + String(static_cast<unsigned long>(DS18B20_values[sensorIndex].timestamp));
  payload += "}";
  return payload;
}

String mqttSensorTopicBase(int sensorIndex) {
  return String(MQTT_TOPIC_SENSOR) + "/" + formatDeviceId(DS18B20_DEVICES[sensorIndex]);
}

String mqttSensorMetaTopic(int sensorIndex) {
  return mqttSensorTopicBase(sensorIndex) + "/meta";
}

String mqttSensorStateTopic(int sensorIndex) {
  return mqttSensorTopicBase(sensorIndex) + "/state";
}

void publishMqttSensorMeta(int sensorIndex) {
  if (!mqttClient.connected()) {
    return;
  }
  String topic = mqttSensorMetaTopic(sensorIndex);
  String payload = mqttSensorMetaPayload(sensorIndex);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

void publishMqttAllSensorsMeta() {
  for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
    publishMqttSensorMeta(i);
  }
}

void publishMqttSensorState(int sensorIndex) {
  if (!mqttClient.connected() || !DS18B20_values[sensorIndex].hasActualValue()) {
    return;
  }
  String topic = mqttSensorStateTopic(sensorIndex);
  String payload = mqttSensorStatePayload(sensorIndex);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

void publishMqttAllSensorsState() {
  for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
    publishMqttSensorState(i);
  }
}

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

IPAddress ip DEVICE_STATIC_IP; //Node static IP
IPAddress gateway DEVICE_GATEWAY_IP;
IPAddress subnet DEVICE_SUBNET_MASK;
IPAddress dnsAddr DEVICE_DNS_IP;

AsyncWebServer server(80); // Порт 80


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

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setClientId(MQTT_CLIENT_ID);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASSWORD);
  mqttClient.setKeepAlive(30);
  mqttClient.setWill(MQTT_TOPIC_STATUS, 1, true, "offline");
  connectMqtt();

  ArduinoOTA.setHostname("heating-controller");
  
  ArduinoOTA.onStart([]() {
    Serial.println("ArduinoOTA start update");
  });

  ArduinoOTA.begin();

  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  
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
    if (
      DS18B20_INPUT >= 0 &&
      DS18B20_INPUT < ONE_WIRE_NUM_DEVICES &&
      DS18B20_values[DS18B20_INPUT].hasActualValue()
    ) {
      doc["inputTemp"] = DS18B20_values[DS18B20_INPUT].temperature;
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
      if (
        tempIndex >= 0 &&
        tempIndex < ONE_WIRE_NUM_DEVICES &&
        DS18B20_values[tempIndex].hasActualValue()
      ) {
        channel["returnTemp"] = DS18B20_values[tempIndex].temperature;
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

      if (DS18B20_values[i].hasActualValue()){
        values[id] = DS18B20_values[i].temperature;
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
    DS18B20_values[i].temperature = 0;
    DS18B20_values[i].timestamp = 0;
  }
  DS18B20.begin();
  DS18B20.setResolution(10);

}

bool isValidT(float t){
  return t > -55 && t < 85;
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

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      if (m - lastMqttReconnect >= MQTT_RECONNECT_PERIOD_MS) {
        lastMqttReconnect = m;
        connectMqtt();
      }
    }
  }

  if (mqttClient.connected()) {
    if (!mqttWasConnected) {
      mqttWasConnected = true;
      publishMqttStartMessages();
    }
  } else {
    mqttWasConnected = false;
  }

  ensureRoomTemperatureCache();
  int p = m % DS18B20_REQUEST_PERIOD;
  if (p == 0){
    int n = (m / DS18B20_REQUEST_PERIOD) % DS18B20_NUM_REQUESTS;
    if (n != prev_n){
      prev_n = n;
      if (n == 0){
        // Full read cycle is complete; publish consolidated sensor state once.
        publishMqttAllSensorsState();
        DS18B20.requestTemperatures();
      }else{
        float t = DS18B20.getTempC(DS18B20_DEVICES[n-1]);
        if (isValidT(t)){
          DS18B20_values[n-1].temperature = t;
          DS18B20_values[n-1].timestamp = time(nullptr);
        }
      }
    }
  }
  
  ArduinoOTA.handle();
  yield();
}
