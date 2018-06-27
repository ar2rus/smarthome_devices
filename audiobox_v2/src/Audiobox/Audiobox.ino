#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

//#include "ESPAsyncTCP.h"
//#include "ESPAsyncWebServer.h"

#include "ClunetMulticast.h"
#include "Credentials.h"

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 124); //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

//AsyncWebServer server(80);


void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.setHostname("audiobox");

  ArduinoOTA.onStart([]() {
    Serial.println("OTA started");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA finished");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\nError[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("OTA auth failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("OTA begin failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("OTA connect failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("OTA receive failed");
    else if (error == OTA_END_ERROR) Serial.println("OTA end failed");
  });

  ArduinoOTA.begin();

  clunetMulticastBegin();

  //server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
  //  char r = 404;
  //  if (request->args() == 0) {
  //    if (switch_toggle(true)){
  //      r = 200;
  //    }
  //  }
  //  server_response(request, r);

   //request->send_P(200, "text/html", index_html);
   
  //});

  //server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
  //  request->send(200, "text/plain", String(ESP.getFreeHeap()));
  //});

 // server.onNotFound( [](AsyncWebServerRequest *request) {
 //   server_response(request, 404);
 // });

 // server.begin();
}



void loop() {
  clunet_msg msg;
  if (clunetMulticastHandleMessages(&msg)) {
    switch (msg.command) {
    }
  }

  ArduinoOTA.handle();
  yield();
}
