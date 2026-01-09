/**
    Use 3.1.2 esp8266 core
    lwip v2 Higher bandwidth; CPU 80 MHz
    4M (FS: 1Mb OTA:~1019Kb)

    dependencies:
    https://github.com/me-no-dev/ESPAsyncWebServer
    https://github.com/ar2rus/ClunetMulticast
    Adafruit SHT31 Library

 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include "BathFan.h"
#include "Credentials.h"

#include <ESPInputs.h>

//#include <ArduinoJson.h>

#include <time.h>
#include <LittleFS.h>  // Используем LittleFS

//#include <vector>
#include "HumidityAutoController.h"

Inputs inputs;

#include "Adafruit_SHT31.h"
//#include <Wire.h>
//#include <Adafruit_Sensor.h>
//#include <Adafruit_BME280.h>


const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 3, 128); //Node static IP
IPAddress gateway(192, 168, 3, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsAddr(192, 168, 3, 1);

AsyncWebServer server(80); // Порт 80
ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);

Adafruit_SHT31 sht31 = Adafruit_SHT31();
//Adafruit_BME280 bme;


static int measure_period = 100; //ms
static float tempOffset = -1.6; // <-- тут оффсет температуры, если датчик перегрет

float t, correctedT, h, correctedH;

HumidityAutoController::Params humParams;

HumidityAutoController humidityCtrl(
  humParams,
  []() {
    char data = 0x02;
    clunet.send(0x90, CLUNET_COMMAND_FAN, &data, 1);
  }
);

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
  
  if (clunet.connect()){
    clunet.onPacketReceived([](clunet_packet* packet){
       switch (packet->command) {
       }
    });
  }

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

// Эндпоинт для получения статистики влажности
server.on("/humidity_stats", HTTP_GET, [](AsyncWebServerRequest *request){
  unsigned long now = millis();

  String response = "{";
  response += "\"state\":\"" + String(humidityCtrl.stateString()) + "\",";
  response += "\"raw\":" + String(humidityCtrl.raw(), 2) + ",";
  response += "\"filtered\":" + String(humidityCtrl.filtered(), 2) + ",";
  response += "\"baseline\":" + String(humidityCtrl.baseline(), 2) + ",";
  response += "\"delta\":" + String(humidityCtrl.delta(), 2) + ",";
  response += "\"growth_rate\":" + String(humidityCtrl.growthRate(now), 3) + ",";
  response += "\"confirm_ms\":" + String(humidityCtrl.confirmTime(now)) + ",";
  response += "\"cooldown_ms\":" + String(humidityCtrl.cooldownLeft(now));
  response += "}";

  request->send(200, "application/json", response);
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
  inputs.on(BUTTON_PIN, STATE_LOW, BUTTON_TIMEOUT, [](uint8_t state){
      clunet.send(0x90, CLUNET_COMMAND_FAN, NULL, 0);
  });

  if (!sht31.begin(0x44)) { // Адрес по умолчанию для SHT31
    Serial.println("Не удалось найти датчик SHT31 :(");
  }

//   Wire.begin(4, 5);
//   if (!bme.begin(0x76)) {  // Адрес 0x76 или 0x77
//    Serial.println("Не удалось найти BME280");
//    while (1);
//  }
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
  if (t_now - t_measure >= measure_period){
    t_measure = t_now;
    float temperature = sht31.readTemperature();
    float humidity = sht31.readHumidity();
    if (!isnan(temperature) && !isnan(humidity)) {
      t = temperature;
      correctedT = t + tempOffset;
      h = humidity;
      correctedH = correctHumidity(h, t, tempOffset);

      humidityCtrl.update(correctedH, t_now);

    }
  }
    
  ArduinoOTA.handle();
  yield();
}
