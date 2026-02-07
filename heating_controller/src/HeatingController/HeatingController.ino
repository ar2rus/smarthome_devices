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

#include "HeatingController.h"
#include "Credentials.h"

#include <ArduinoJson.h>

#include <time.h>
#include <LittleFS.h>  // Используем LittleFS


OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature DS18B20(&oneWire);

float DS18B20_values[ONE_WIRE_NUM_DEVICES];

int DS18B20_errors[ONE_WIRE_NUM_DEVICES];
long DS18B20_last[ONE_WIRE_NUM_DEVICES];
long DS18B20_unavailables[ONE_WIRE_NUM_DEVICES];


bool relay_states[RELAY_NUM];

void relay_state(int index, bool _on){
  if (relay_states[index] != _on){
     relay_states[index] = _on;
     apply_relay_state(index);
  }
}

void apply_relay_state(int index){
  digitalWrite(RELAY_PIN[index], relay_states[index] ? HIGH : LOW);
}

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 3, 127); //Node static IP
IPAddress gateway(192, 168, 3, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsAddr(192, 168, 3, 1);

AsyncWebServer server(80); // Порт 80
ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);


void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  // Инициализируем выходы для реле
  for (int i=0; i<RELAY_NUM; i++){
    pinMode(RELAY_PIN[i], OUTPUT);
    apply_relay_state(i);
  }

//  // Инициализируем файловую систему LittleFS
//  if (!LittleFS.begin()) {
//    Serial.println("Failed to mount LittleFS");
//    
//    // Даже если не получилось загрузить LittleFS, инициализируем с дефолтными настройками
//    loadSettingFromConfig();
//  } else {
//    Serial.println("LittleFS mounted successfully");
//    // Загружаем настройки из файла
//    loadHeatfloorSettingsFromFile();
//  }
  

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


  server.on("/t", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    
    struct timeval tp;
    gettimeofday(&tp, 0);
    
    doc["time"] = serialized(String(tp.tv_sec) + String(tp.tv_usec /1000UL));
    for (int i=0; i<ONE_WIRE_NUM_DEVICES; i++){
      if (isValidT(DS18B20_values[i])){
        doc["DS18B20_" + String(i+1)] = serialized(String(DS18B20_values[i], 2));
      }
      
      doc["DS18B20_" + String(i+1)+"_stat"] = serialized(String(DS18B20_values[i], 2)) + " (" + String(DS18B20_errors[i]) + " / " + String(DS18B20_unavailables[i]) + ")";
    }

    String json;
    serializeJson(doc, json);
    
    request->send(200, "application/json", json);
  });

  server.on("/r", HTTP_GET, [](AsyncWebServerRequest *request){
    int i = request->arg("i").toInt();
    relay_state(i, !relay_states[i]);
    
    request->send(200, "text/plain", String(relay_states[i]));
  });

  
//  // Обработчик для корневого маршрута - отдаем index.html
//  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
//    request->send(LittleFS, "/index.html", "text/html");
//  });
  
//  // Обработчик для загрузки статических файлов (CSS, JS, изображения)
//  server.serveStatic("/", LittleFS, "/");
  
  server.onNotFound( [](AsyncWebServerRequest *request) {
    server_response(request, 404);
  });

  server.begin();

  for (int i=0; i<ONE_WIRE_NUM_DEVICES; i++){
    DS18B20_values[i] = DEVICE_DISCONNECTED_C;
  }
  DS18B20.begin();
  DS18B20.setResolution(12);

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
  int p = m % DS18B20_REQUEST_PERIOD;
  if (p == 0){
    int n = (m / DS18B20_REQUEST_PERIOD) % DS18B20_NUM_REQUESTS;
    if (n != prev_n){
      prev_n = n;
      if (n == 0){
        DS18B20.requestTemperatures();
      }else{
        DS18B20_values[n-1] = DS18B20.getTempC(DS18B20_DEVICES[n-1]);
        if (isValidT(DS18B20_values[n-1])){
          DS18B20_last[n-1] = m;
        }else{
          DS18B20_errors[n-1]++;
          if (m-DS18B20_last[n-1] > DS18B20_unavailables[n-1]){
            DS18B20_unavailables[n-1] = m-DS18B20_last[n-1];
          }
        }
      }
    }
  }
  
  ArduinoOTA.handle();
  yield();
}
