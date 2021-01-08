/**
 * Use 2.6.3 esp8266 core
 * lwip 1.4 Higher bandwidth; CPU 80 MHz
 * 4Mb/FS:2Mb/OTA:1019Kb !!!
 * 
 * dependencies:
 * https://github.com/mandulaj/PZEM-004T-v30
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <LittleFS.h>

#include <ArduinoJson.h>
#include <PZEM004Tv30.h>

#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include <ESPInputs.h>

#include "Energy.h"
#include "Credentials.h"

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 126);     //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer server(80);
PZEM004Tv30 pzem(&Serial);
ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);

Inputs inputs;


//void send_clunet_device_state_info(uint8_t address){
//  char data[] = {3, *(char*)&mirobo_state};
//  clunet.send(address, CLUNET_COMMAND_DEVICE_STATE_INFO, data, sizeof(data));
//}

float voltage;
float current;
float power;
float energy;
float frequency;
float pf;

void config_time(float timezone_hours_offset, int daylightOffset_sec,
                 const char* server1, const char* server2, const char* server3) {
  configTime((int)(timezone_hours_offset * 3600), daylightOffset_sec, server1, server2, server3);
}

void setup() {
  Serial1.begin(115200);
  
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(1000);
    ESP.restart();
  }

  if (!LittleFS.begin()) {
    Serial1.println("LittleFS mount failed");
    return;
  }

  ArduinoOTA.setHostname("energy-meter");
  
  ArduinoOTA.onStart([]() {
    Serial1.println("ArduinoOTA start update");
    if (ArduinoOTA.getCommand() == U_FS) {
      LittleFS.end();
    }   
  });

  ArduinoOTA.begin();
 
  config_time(0, 0, "pool.ntp.org", "time.nist.gov", NULL);

  server.on("/voltage", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(voltage, 1));  
  });
  
  server.on("/current", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(current, 3));  
  });
  
  server.on("/power", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(power, 1));  
  });
  
  server.on("/energy", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(energy, 3));  
  });
  
  server.on("/frequency", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(frequency, 1));  
  });
  
  server.on("/pf", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(pf, 2));  
  });
               
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(128);
    doc["time"] = time(NULL);
    doc["voltage"] = voltage;
    doc["current"] = current;
    doc["power"] = power;
    doc["energy"] = energy;
    doc["frequency"] = frequency;
    doc["pf"] = pf;

    String json;
    serializeJson(doc, json);
    
    request->send(200, "application/json", json);  
  });

  server.begin();

  if (clunet.connect()){
    clunet.onPacketReceived([](clunet_packet* packet){
      
      /*switch (msg->command) {
       
        case CLUNET_COMMAND_BUTTON_INFO: {
          if (msg->src_address == 0x1D){
            if (msg->size == 2 && msg->data[0] == 02 && msg->data[1] == 01){
              tasker.once(TASKER_GROUP_MIIO, &mirobo_toggle);
            }
          }
          break;
        }
      }*/
    });
  }

  inputs.on(RESET_ENERGY_BUTTON_PIN, STATE_LOW, RESET_ENERGY_BUTTON_TIMEOUT, [](uint8_t state){
    pzem.resetEnergy();
  });
  
}


unsigned long pzem_update_t = 0;
unsigned long button_pressed_t = 0;

void loop() {
  unsigned long ct = millis();
  if (ct - pzem_update_t >= PZEM_UPDATE_TIMEOUT){
    pzem_update_t = ct;

    voltage = pzem.voltage();
    current = pzem.current();
    power = pzem.power();
    energy = pzem.energy();
    frequency = pzem.frequency();
    pf = pzem.pf();
  }

  inputs.handle();
  
  ArduinoOTA.handle();
  yield();
}
