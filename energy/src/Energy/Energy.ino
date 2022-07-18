/**
 * Use 3.0.2 esp8266 core
 * lwip v2 Higher bandwidth; CPU 80 MHz
 * 4Mb/FS:2Mb/OTA:1019Kb !!!
 * 
 * dependencies:
 * https://github.com/mandulaj/PZEM-004T-v30
 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <ArduinoJson.h>
#include <PZEM004Tv30.h>

#include <ESPInputs.h>

#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include "Energy.h"
#include "Credentials.h"

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 126);     //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsAddr(192, 168, 1, 1);

AsyncWebServer server(80);
ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);

Inputs inputs;

PZEM004Tv30 pzem(Serial);
pzemValues values;

void setup() {
  Serial1.begin(115200);
  
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet, dnsAddr);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(1000);
    ESP.restart();
  }
  
  ArduinoOTA.setHostname("energy-meter");
  
  ArduinoOTA.onStart([]() {
    Serial1.println("ArduinoOTA start update");
  });

  ArduinoOTA.begin();

  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  server.on("/voltage", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(values.voltage, 1));  
  });
  
  server.on("/current", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(values.current, 3));  
  });
  
  server.on("/power", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(values.power, 1));  
  });
  
  server.on("/energy", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(values.energy, 3));  
  });
  
  server.on("/frequency", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(values.frequency, 1));  
  });
  
  server.on("/pf", HTTP_GET, [](AsyncWebServerRequest* request) {
     request->send(200, "text/plain", String(values.pf, 2));  
  });
               
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(256);
    
    struct timeval tp;
    gettimeofday(&tp, 0);
    
    doc["time"] = serialized(String(tp.tv_sec) + String(tp.tv_usec /1000UL));
    doc["voltage"] = serialized(String(values.voltage, 1));
    doc["current"] = serialized(String(values.current, 3));
    doc["power"] = serialized(String(values.power, 1));
    doc["energy"] = values.energy;
    doc["frequency"] = serialized(String(values.frequency, 1));
    doc["pf"] = serialized(String(values.pf, 1));

    String json;
    serializeJson(doc, json);
    
    request->send(200, "application/json", json);  
  });

  server.begin();

  if (clunet.connect()){
    clunet.onPacketReceived([](clunet_packet* packet){
    });
  }

  inputs.on(RESET_ENERGY_BUTTON_PIN, STATE_LOW, RESET_ENERGY_BUTTON_TIMEOUT, [](uint8_t state){
    pzem.resetEnergy();
  });  
}

void read_pzem_values(){
  values.voltage = pzem.voltage();
  values.current = pzem.current();
  values.power = pzem.power();
  values.energy = pzem.energy();
  values.frequency = pzem.frequency();
  values.pf = pzem.pf();
}

void loop() {
  read_pzem_values();
  
  inputs.handle();
  ArduinoOTA.handle();
  yield();
}
