/**
    Use 3.1.2 esp8266 core
    lwip v2 Higher bandwidth; CPU 80 MHz
    4M (FS: 1Mb OTA:~1019Kb)

    dependencies:
    https://github.com/me-no-dev/ESPAsyncWebServer
    Adafruit SHT31 Library
    AsyncMqttClient

 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>

#include "BathFan.h"
#include "Credentials.h"


#include <time.h>
#include <LittleFS.h>  // Используем LittleFS
#include <ESPInputs.h>
#include <EEPROM.h>

#include "HumidityAutoController.h"
#include "Adafruit_SHT31.h"


const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip DEVICE_STATIC_IP;
IPAddress gateway DEVICE_GATEWAY_IP;
IPAddress subnet DEVICE_SUBNET_MASK;
IPAddress dnsAddr DEVICE_DNS_IP;

AsyncWebServer server(80); // Порт 80

AsyncMqttClient mqttClient;

Adafruit_SHT31 sht31 = Adafruit_SHT31();
Inputs inputs;


static int measure_period = 100; //ms
static float tempOffset = -1.6; // <-- тут оффсет температуры, если датчик перегрет

static unsigned long mqttPublishPeriodMs = 5000;
static unsigned long mqttReconnectPeriodMs = 5000;
static bool littleFsMounted = false;
static bool eepromReady = false;

constexpr uint16_t HUM_PARAMS_EEPROM_SIZE = 512;
constexpr uint16_t HUM_PARAMS_EEPROM_ADDR = 0;
constexpr uint32_t HUM_PARAMS_EEPROM_MAGIC = 0x42504631UL; // "BPF1"
constexpr uint16_t HUM_PARAMS_EEPROM_VERSION = 1;

float t, correctedT, h, correctedH;
unsigned long lastMqttPublish = 0;
unsigned long lastMqttReconnect = 0;
bool mqttWasConnected = false;

HumidityAutoController::Params humParams;
void publishHumidityControllerEvent(HumidityAutoController::State state);
void publishFanOnCommand();
void publishFanToggleCommand();
String jsonFloatOrNull(float value, int decimals);
String paramsJson(const HumidityAutoController::Params& params);
String diagnosticsStateJson(unsigned long nowMs);
void registerDiagnosticsRoutes();
void clampHumidityParams(HumidityAutoController::Params& params);
bool loadHumidityParamsFromEeprom(HumidityAutoController::Params& outParams);
bool saveHumidityParamsToEeprom(const HumidityAutoController::Params& params);
uint32_t hashFnv1a(const uint8_t* data, size_t len);

struct PersistedHumidityParams {
  uint32_t magic;
  uint16_t version;
  uint16_t payloadSize;
  HumidityAutoController::Params params;
  uint32_t checksum;
};

HumidityAutoController humidityCtrl(
  humParams,
  [](HumidityAutoController::State state, unsigned long) {
    if (state == HumidityAutoController::State::TRIGGERED) {
      publishFanOnCommand();
    }
    publishHumidityControllerEvent(state);
  }
);

String sht31Meta() {
  String payload = "{";
  payload += "\"type\":\"SHT31\",";
  payload += "\"units\":{";
  payload += "\"temperature\":\"C\",";
  payload += "\"humidity\":\"%\",";
  payload += "\"raw_temperature\":\"C\",";
  payload += "\"raw_humidity\":\"%\"";
  payload += "},";
  payload += "\"fields\":{";
  payload += "\"temperature\":\"corrected\",";
  payload += "\"humidity\":\"corrected\",";
  payload += "\"raw_temperature\":\"raw\",";
  payload += "\"raw_humidity\":\"raw\"";
  payload += "}";
  payload += "}";
  return payload;
}

String deviceMeta() {
  String payload = "{";
  payload += "\"location\":\"" + String(MQTT_SENSOR_LOCATION) + "\"";
  payload += "}";
  return payload;
}

void publishDeviceMeta() {
  if (mqttClient.connected()) {
    String payload = deviceMeta();
    mqttClient.publish(MQTT_TOPIC_DEVICE_META, 0, true, payload.c_str());
  }
}

void publishSht31Meta() {
  if (mqttClient.connected()) {
    String payload = sht31Meta();
    mqttClient.publish(MQTT_TOPIC_SHT31_META, 0, true, payload.c_str());
  }
}

void publishMqttStartMessages() {
  mqttClient.publish(MQTT_TOPIC_STATUS, 1, true, "online");
  publishDeviceMeta();
  publishSht31Meta();
}

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!mqttClient.connected()) {
    mqttClient.connect();
  }
}

bool isValidSht31Reading(float temperature, float humidity) {
  if (!isfinite(temperature) || !isfinite(humidity)) {
    return false;
  }
  if (temperature < -40.0f || temperature > 125.0f) {
    return false;
  }
  if (humidity < 0.0f || humidity > 100.0f) {
    return false;
  }
  return true;
}

bool isNTPReady(time_t nowSec) {
  return nowSec > 100000;
}

unsigned long currentTimestampSec(unsigned long nowMs) {
  time_t nowSec = time(nullptr);
  if (isNTPReady(nowSec)) {
    return static_cast<unsigned long>(nowSec);
  }
  return nowMs / 1000UL;
}

String sht31State(float temperature, float humidity, float rawTemperature, float rawHumidity, unsigned long nowMs) {
  String payload = "{";
  payload += "\"temperature\":" + String(temperature, 2) + ",";
  payload += "\"humidity\":" + String(humidity, 2) + ",";
  payload += "\"raw_temperature\":" + String(rawTemperature, 2) + ",";
  payload += "\"raw_humidity\":" + String(rawHumidity, 2) + ",";
  payload += "\"timestamp\":" + String(currentTimestampSec(nowMs));
  payload += "}";
  return payload;
}

void publishSht31State(float temperature, float humidity, float rawTemperature, float rawHumidity, unsigned long nowMs) {
  if (mqttClient.connected()) {
    String payload = sht31State(temperature, humidity, rawTemperature, rawHumidity, nowMs);
    mqttClient.publish(MQTT_TOPIC_SHT31_STATE, 0, true, payload.c_str());
  }
}

String humidityControllerState(unsigned long nowMs) {
  String payload = "{";
  payload += "\"state\":\"" + String(HumidityAutoController::stateString(humidityCtrl.state())) + "\",";
  payload += "\"raw\":" + String(humidityCtrl.raw(), 2) + ",";
  payload += "\"filtered\":" + String(humidityCtrl.filtered(), 2) + ",";
  payload += "\"baseline\":" + String(humidityCtrl.baseline(), 2) + ",";
  payload += "\"delta\":" + String(humidityCtrl.delta(), 2) + ",";
  payload += "\"growth_rate\":" + String(humidityCtrl.growthRate(nowMs), 3) + ",";
  payload += "\"confirm_ms\":" + String(humidityCtrl.confirmTime(nowMs)) + ",";
  payload += "\"cooldown_ms\":" + String(humidityCtrl.cooldownLeft(nowMs)) + ",";
  payload += "\"timestamp\":" + String(currentTimestampSec(nowMs));
  payload += "}";
  return payload;
}

void publishHumidityControllerState(unsigned long nowMs) {
  if (mqttClient.connected()) {
    String payload = humidityControllerState(nowMs);
    mqttClient.publish(MQTT_TOPIC_HUMIDITY_CONTROLLER_STATE, 0, true, payload.c_str());
  }
}

void publishHumidityControllerEvent(HumidityAutoController::State state) {
  if (mqttClient.connected()) {
    const char* stateName = HumidityAutoController::stateString(state);
    mqttClient.publish(MQTT_TOPIC_HUMIDITY_CONTROLLER_EVENT, 0, false, stateName);
  }
}

void publishButtonPressEvent() {
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC_BUTTON_EVENT, 1, false, "PRESS");
  }
}

void publishFanOnCommand() {
  if (mqttClient.connected()) {
    String payload = "{\"durationMinutes\":" + String(FAN_BATHROOM_ON_DURATION_MINUTES) + "}";
    mqttClient.publish(MQTT_TOPIC_FAN_BATHROOM_ON, 1, false, payload.c_str());
  }
}

void publishFanToggleCommand() {
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC_FAN_BATHROOM_TOGGLE, 1, false, "1");
  }
}

uint32_t hashFnv1a(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint32_t>(data[i]);
    hash *= 16777619UL;
  }
  return hash;
}

void clampHumidityParams(HumidityAutoController::Params& params) {
  params.emaAlpha = constrain(params.emaAlpha, 0.001f, 1.0f);
  params.baselineAlpha = constrain(params.baselineAlpha, 0.0001f, 1.0f);
  params.triggerDelta = constrain(params.triggerDelta, 0.1f, 50.0f);
  params.triggerRate = constrain(params.triggerRate, 0.01f, 10.0f);
  params.confirmMs = static_cast<unsigned long>(constrain(static_cast<long>(params.confirmMs), 0L, 120000L));
  params.cooldownMs = static_cast<unsigned long>(constrain(static_cast<long>(params.cooldownMs), 0L, 7200000L));
  params.windowMs = static_cast<unsigned long>(constrain(static_cast<long>(params.windowMs), 1000L, 120000L));
  params.bufferSize = constrain(params.bufferSize, 20, 600);
}

bool loadHumidityParamsFromEeprom(HumidityAutoController::Params& outParams) {
  if (!eepromReady) {
    return false;
  }

  PersistedHumidityParams stored{};
  EEPROM.get(HUM_PARAMS_EEPROM_ADDR, stored);

  if (stored.magic != HUM_PARAMS_EEPROM_MAGIC ||
      stored.version != HUM_PARAMS_EEPROM_VERSION ||
      stored.payloadSize != sizeof(HumidityAutoController::Params)) {
    return false;
  }

  PersistedHumidityParams check = stored;
  check.checksum = 0;
  uint32_t expected = hashFnv1a(
    reinterpret_cast<const uint8_t*>(&check),
    sizeof(PersistedHumidityParams)
  );
  if (stored.checksum != expected) {
    return false;
  }

  outParams = stored.params;
  clampHumidityParams(outParams);
  return true;
}

bool saveHumidityParamsToEeprom(const HumidityAutoController::Params& params) {
  if (!eepromReady) {
    return false;
  }

  PersistedHumidityParams stored{};
  stored.magic = HUM_PARAMS_EEPROM_MAGIC;
  stored.version = HUM_PARAMS_EEPROM_VERSION;
  stored.payloadSize = sizeof(HumidityAutoController::Params);
  stored.params = params;
  clampHumidityParams(stored.params);
  stored.checksum = 0;
  stored.checksum = hashFnv1a(
    reinterpret_cast<const uint8_t*>(&stored),
    sizeof(PersistedHumidityParams)
  );

  EEPROM.put(HUM_PARAMS_EEPROM_ADDR, stored);
  return EEPROM.commit();
}

String jsonFloatOrNull(float value, int decimals) {
  if (!isfinite(value)) {
    return "null";
  }
  return String(value, decimals);
}

String paramsJson(const HumidityAutoController::Params& params) {
  String payload = "{";
  payload += "\"emaAlpha\":" + String(params.emaAlpha, 4) + ",";
  payload += "\"baselineAlpha\":" + String(params.baselineAlpha, 4) + ",";
  payload += "\"triggerDelta\":" + String(params.triggerDelta, 3) + ",";
  payload += "\"triggerRate\":" + String(params.triggerRate, 3) + ",";
  payload += "\"confirmMs\":" + String(params.confirmMs) + ",";
  payload += "\"cooldownMs\":" + String(params.cooldownMs) + ",";
  payload += "\"windowMs\":" + String(params.windowMs) + ",";
  payload += "\"bufferSize\":" + String(params.bufferSize);
  payload += "}";
  return payload;
}

String diagnosticsStateJson(unsigned long nowMs) {
  const HumidityAutoController::Params& params = humidityCtrl.params();
  float filtered = humidityCtrl.filtered();
  float baseline = humidityCtrl.baseline();
  float delta = humidityCtrl.delta();
  float growth = humidityCtrl.growthRate(nowMs);
  bool deltaReached = isfinite(delta) && delta >= params.triggerDelta;
  bool rateReached = isfinite(growth) && growth >= params.triggerRate;

  String payload = "{";
  payload += "\"timestamp\":" + String(currentTimestampSec(nowMs)) + ",";
  payload += "\"uptimeMs\":" + String(nowMs) + ",";
  payload += "\"state\":\"" + String(HumidityAutoController::stateString(humidityCtrl.state())) + "\",";
  payload += "\"wifiConnected\":";
  payload += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
  payload += ",";
  payload += "\"sensor\":{";
  payload += "\"temperature\":" + jsonFloatOrNull(correctedT, 2) + ",";
  payload += "\"rawTemperature\":" + jsonFloatOrNull(t, 2) + ",";
  payload += "\"humidity\":" + jsonFloatOrNull(correctedH, 2) + ",";
  payload += "\"rawHumidity\":" + jsonFloatOrNull(h, 2);
  payload += "},";
  payload += "\"controller\":{";
  payload += "\"raw\":" + jsonFloatOrNull(humidityCtrl.raw(), 2) + ",";
  payload += "\"filtered\":" + jsonFloatOrNull(filtered, 2) + ",";
  payload += "\"baseline\":" + jsonFloatOrNull(baseline, 2) + ",";
  payload += "\"delta\":" + jsonFloatOrNull(delta, 2) + ",";
  payload += "\"growthRate\":" + jsonFloatOrNull(growth, 3) + ",";
  payload += "\"confirmMs\":" + String(humidityCtrl.confirmTime(nowMs)) + ",";
  payload += "\"cooldownMs\":" + String(humidityCtrl.cooldownLeft(nowMs));
  payload += "},";
  payload += "\"thresholds\":{";
  payload += "\"triggerDelta\":" + String(params.triggerDelta, 3) + ",";
  payload += "\"triggerRate\":" + String(params.triggerRate, 3);
  payload += "},";
  payload += "\"conditions\":{";
  payload += "\"deltaReached\":";
  payload += deltaReached ? "true" : "false";
  payload += ",";
  payload += "\"rateReached\":";
  payload += rateReached ? "true" : "false";
  payload += "},";
  payload += "\"params\":";
  payload += paramsJson(params);
  payload += "}";
  return payload;
}

void registerDiagnosticsRoutes() {
  auto serveInsightsPage = [](AsyncWebServerRequest *request) {
    if (!littleFsMounted) {
      request->send(503, "text/plain", "LittleFS is not mounted");
      return;
    }
    request->send(LittleFS, "/diagnostics.html", "text/html");
  };

  server.on("/", HTTP_GET, serveInsightsPage);
  server.on("/insights/humidity", HTTP_GET, serveInsightsPage);
  server.on("/insights/humidity/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/insights/humidity");
  });

  auto sendCurrentState = [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", diagnosticsStateJson(millis()));
  };
  auto sendParams = [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", paramsJson(humidityCtrl.params()));
  };

  server.on("/api/insights/humidity/current", HTTP_GET, sendCurrentState);
  server.on("/api/insights/humidity/params", HTTP_GET, sendParams);

  auto updateParams = [](AsyncWebServerRequest *request) {
    HumidityAutoController::Params next = humidityCtrl.params();
    bool hasAny = false;

    if (request->hasParam("emaAlpha", true)) {
      next.emaAlpha = constrain(request->getParam("emaAlpha", true)->value().toFloat(), 0.001f, 1.0f);
      hasAny = true;
    }
    if (request->hasParam("baselineAlpha", true)) {
      next.baselineAlpha = constrain(request->getParam("baselineAlpha", true)->value().toFloat(), 0.0001f, 1.0f);
      hasAny = true;
    }
    if (request->hasParam("triggerDelta", true)) {
      next.triggerDelta = constrain(request->getParam("triggerDelta", true)->value().toFloat(), 0.1f, 50.0f);
      hasAny = true;
    }
    if (request->hasParam("triggerRate", true)) {
      next.triggerRate = constrain(request->getParam("triggerRate", true)->value().toFloat(), 0.01f, 10.0f);
      hasAny = true;
    }
    if (request->hasParam("confirmMs", true)) {
      long value = request->getParam("confirmMs", true)->value().toInt();
      next.confirmMs = static_cast<unsigned long>(constrain(value, 0L, 120000L));
      hasAny = true;
    }
    if (request->hasParam("cooldownMs", true)) {
      long value = request->getParam("cooldownMs", true)->value().toInt();
      next.cooldownMs = static_cast<unsigned long>(constrain(value, 0L, 7200000L));
      hasAny = true;
    }
    if (request->hasParam("windowMs", true)) {
      long value = request->getParam("windowMs", true)->value().toInt();
      next.windowMs = static_cast<unsigned long>(constrain(value, 1000L, 120000L));
      hasAny = true;
    }
    if (request->hasParam("bufferSize", true)) {
      int value = request->getParam("bufferSize", true)->value().toInt();
      next.bufferSize = constrain(value, 20, 600);
      hasAny = true;
    }

    if (!hasAny) {
      request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid parameters\"}");
      return;
    }

    clampHumidityParams(next);
    if (!humidityCtrl.setParams(next)) {
      request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid parameters\"}");
      return;
    }

    humParams = next;

    bool persisted = saveHumidityParamsToEeprom(next);
    String response = "{\"success\":true,\"params\":";
    response += paramsJson(humidityCtrl.params());
    response += ",\"persisted\":";
    response += persisted ? "true" : "false";
    response += "}";
    request->send(200, "application/json", response);
  };

  server.on("/api/insights/humidity/params", HTTP_POST, updateParams);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");


  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet, dnsAddr);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(1000);
    ESP.restart();
  }

  ArduinoOTA.setHostname("bath-fan");
  
  ArduinoOTA.onStart([]() {
    Serial.println("ArduinoOTA start update");
  });

  ArduinoOTA.begin();

  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  EEPROM.begin(HUM_PARAMS_EEPROM_SIZE);
  eepromReady = true;
  Serial.println("EEPROM initialized");

  HumidityAutoController::Params persisted = humParams;
  if (loadHumidityParamsFromEeprom(persisted) && humidityCtrl.setParams(persisted)) {
    humParams = persisted;
    Serial.println("Humidity params loaded from EEPROM");
  } else {
    Serial.println("Humidity params EEPROM: defaults");
  }

  littleFsMounted = LittleFS.begin();
  if (!littleFsMounted) {
    Serial.println("LittleFS mount failed");
  } else {
    Serial.println("LittleFS mounted");
  }

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setKeepAlive(30);
  mqttClient.setClientId(MQTT_CLIENT_ID);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASSWORD);
  mqttClient.setWill(MQTT_TOPIC_STATUS, 1, true, "offline");
  connectMqtt();

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });


  server.on("/h", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(correctedH) +" (" + String(h) + ")");
  });

  server.on("/t", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(correctedT) + " (" + String(t) + ")");
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    ESP.restart();
  });

  registerDiagnosticsRoutes();

  // Обработчик для корневого маршрута - отдаем index.html
//  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
//    request->send(LittleFS, "/index.html", "text/html");
//  });
  
  // Обработчик для загрузки статических файлов (CSS, JS, изображения)
//  server.serveStatic("/", LittleFS, "/");
  
  server.onNotFound( [](AsyncWebServerRequest *request) {
    server_response(request, 404);
  });

  server.begin();

  // Настройка обработчика кнопки для управления вентилятором туалета
  inputs.on(BUTTON_PIN, STATE_LOW, BUTTON_TIMEOUT, [](uint8_t){
      publishFanToggleCommand();
      publishButtonPressEvent();
  });

  if (!sht31.begin(0x44)) { // Адрес по умолчанию для SHT31
    Serial.println("Не удалось найти датчик SHT31 :(");
  }
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

float correctHumidity(float rawRH, float rawTempC, float tempOffsetC) {
  // Исправленная температура
  float correctedTempC = rawTempC + tempOffsetC;

  // Константы для формулы Магнуса (в диапазоне 0–50 °C)
  const float a = 17.62;
  const float b = 243.12;

  // Насыщенное давление водяного пара при измеренной температуре
  float es_raw = 6.112 * exp((a * rawTempC) / (b + rawTempC));

  // Парциальное давление водяного пара
  float e = (rawRH / 100.0) * es_raw;

  // Насыщенное давление водяного пара при скорректированной температуре
  float es_corr = 6.112 * exp((a * correctedTempC) / (b + correctedTempC));

  // Пересчитанная относительная влажность
  float correctedRH = (e / es_corr) * 100.0;

  // Ограничим результат в диапазоне 0–100%
  if (correctedRH > 100.0) correctedRH = 100.0;
  if (correctedRH < 0.0) correctedRH = 0.0;

  return correctedRH;
}

long t_measure=0;

void loop() {
  inputs.handle();

  unsigned long t_now = millis();
  
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      if (t_now - lastMqttReconnect >= mqttReconnectPeriodMs) {
        lastMqttReconnect = t_now;
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

  if (t_now - t_measure >= measure_period){
    t_measure = t_now;
    float temperature = sht31.readTemperature();
    float humidity = sht31.readHumidity();
    if (isValidSht31Reading(temperature, humidity)) {
      t = temperature;
      correctedT = t + tempOffset;
      h = humidity;
      correctedH = correctHumidity(h, t, tempOffset);

      if (isfinite(correctedT) && isfinite(correctedH)) {
        humidityCtrl.update(correctedH, t_now);

        if (mqttClient.connected() && (t_now - lastMqttPublish >= mqttPublishPeriodMs)) {
          lastMqttPublish = t_now;
          publishSht31State(correctedT, correctedH, t, h, t_now);
          publishHumidityControllerState(t_now);
        }
      }

    }
  }
    
  ArduinoOTA.handle();
  yield();
}
