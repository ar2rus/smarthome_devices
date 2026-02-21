/**
    Use 3.1.2 esp8266 core
    lwip v2 Higher bandwidth; CPU 80 MHz
    4M (FS: 1Mb OTA:~1019Kb)

    dependencies:
    https://github.com/me-no-dev/ESPAsyncWebServer
    Adafruit SHT31 Library
    PubSubClient

 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>

#include "BathFan.h"
#include "Credentials.h"


#include <time.h>
#include <LittleFS.h>  // Используем LittleFS
#include <ESPInputs.h>

#include "HumidityAutoController.h"
#include "Adafruit_SHT31.h"


const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip DEVICE_STATIC_IP;
IPAddress gateway DEVICE_GATEWAY_IP;
IPAddress subnet DEVICE_SUBNET_MASK;
IPAddress dnsAddr DEVICE_DNS_IP;

AsyncWebServer server(80); // Порт 80

WiFiClient mqttTransport;
PubSubClient mqttClient(mqttTransport);

Adafruit_SHT31 sht31 = Adafruit_SHT31();
Inputs inputs;


static int measure_period = 100; //ms
static float tempOffset = -1.6; // <-- тут оффсет температуры, если датчик перегрет

static unsigned long mqttPublishPeriodMs = 5000;
static unsigned long mqttReconnectPeriodMs = 5000;

float t, correctedT, h, correctedH;
unsigned long lastMqttPublish = 0;
unsigned long lastMqttReconnect = 0;

HumidityAutoController::Params humParams;
void publishHumidityControllerEvent(HumidityAutoController::State state);
String humidityControllerPayload(unsigned long nowMs);
void publishFanOnCommand();
void publishFanToggleCommand();

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
    mqttClient.publish(MQTT_TOPIC_DEVICE_META, payload.c_str(), true);
  }
}

void publishSht31Meta() {
  if (mqttClient.connected()) {
    String payload = sht31Meta();
    mqttClient.publish(MQTT_TOPIC_SHT31_META, payload.c_str(), true);
  }
}

bool connectMqtt() {
  bool connected = mqttClient.connect(
    MQTT_CLIENT_ID,
    MQTT_USER,
    MQTT_PASSWORD,
    MQTT_TOPIC_STATUS,
    1,
    true,
    "offline"
  );

  if (connected) {
    mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);
    publishDeviceMeta();
    publishSht31Meta();
  }
  return connected;
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
    mqttClient.publish(MQTT_TOPIC_SHT31_STATE, payload.c_str(), true);
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
    mqttClient.publish(MQTT_TOPIC_HUMIDITY_CONTROLLER_STATE, payload.c_str(), true);
  }
}

void publishHumidityControllerEvent(HumidityAutoController::State state) {
  if (mqttClient.connected()) {
    const char* stateName = HumidityAutoController::stateString(state);
    mqttClient.publish(MQTT_TOPIC_HUMIDITY_CONTROLLER_EVENT, stateName, false);
  }
}

void publishButtonPressEvent() {
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC_BUTTON_EVENT, "PRESS", false);
  }
}

void publishFanOnCommand() {
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC_FAN_BATHROOM_ON, "1", false);
  }
}

void publishFanToggleCommand() {
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC_FAN_BATHROOM_TOGGLE, "1", false);
  }
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

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setKeepAlive(30);
  mqttClient.setBufferSize(512);
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
    } else {
      mqttClient.loop();
    }
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
