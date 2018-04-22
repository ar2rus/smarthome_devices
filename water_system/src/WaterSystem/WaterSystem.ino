#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "ESPAsyncTCP.h"
#include "ESPAsyncWebServer.h"

#include "ClunetMulticast.h"
#include "Credentials.h"

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 123); //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer server(80);


const int SERVO_PIN = 13;
const int PUMP_PIN = 5;

int pump_state = LOW;

void setup() {
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, pump_state);
  
  pinMode(SERVO_PIN, OUTPUT);
  
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

  ArduinoOTA.setHostname("water-system");

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

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  clunetMulticastBegin();

  //server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
  //  char r = 404;
  //  if (request->args() == 0) {
  //    if (switch_toggle(true)){
  //      r = 200;
  //    }
  //  }
  //  server_response(request, r);
  //});

  server.on("/pump_on", HTTP_GET, [](AsyncWebServerRequest *request){  //on and dimmer
    char r = 404;
      switch (request->args()) {
        case 0:
          if (switch_on(true)){
            r = 200;
          }
          break;
      }
    server_response(request, r);
  });

  server.on("/pump_off", HTTP_GET, [](AsyncWebServerRequest *request){  //off
    char r = 404;
    if (request->args() == 0) {
      if (switch_off(true)){
        r = 200;
      }
    }
    server_response(request, r);
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.onNotFound( [](AsyncWebServerRequest *request) {
    server_response(request, 404);
  });

  server.begin();
}

void server_response(AsyncWebServerRequest *request, unsigned int response) {
  switch (response) {
    case 200:
      request->send(200);
      break;
    default:
      //case 404:
      request->send(404, "text/plain", "File Not Found\n\n");
      break;
  }
}


const char RELAY_0_ID = 1;

void switchResponse(unsigned char address) {
  char info = (pump_state << (RELAY_0_ID - 1));
  clunetMulticastSend(address, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

boolean switchExecute(char command) {
  switch (command) {
    case 0x00:  //откл
      pump_state = LOW;
      break;
    case 0x01: //вкл
      pump_state = HIGH;
      break;
    case 0x02: //перекл
      pump_state = !pump_state;
      break;
    default:
        return false;
  }

  //set value
  digitalWrite(PUMP_PIN, pump_state);
  return true;
}

boolean switch_exec(char command, boolean send_response) {
  boolean r = switchExecute(command);
  if (r) {
    if (send_response) {
      switchResponse(CLUNET_BROADCAST_ADDRESS);
    }
  }
  return r;
}

boolean switch_on(boolean send_response) {
  return switch_exec(0x01, send_response);
}

boolean switch_off(boolean send_response) {
  return switch_exec(0x00, send_response);
}

boolean switch_toggle(boolean send_response) {
  return switch_exec(0x02, send_response);
}




void loop() {
 clunet_msg msg;
  if (clunetMulticastHandleMessages(&msg)) {
    switch (msg.command) {
      case CLUNET_COMMAND_SWITCH:
        if (msg.data[0] == 0xFF) { //info request
          if (msg.size == 1) {
            switchResponse(msg.src_address);
          }
        } else {
          if (msg.size == 2) {
            switch (msg.data[0]) {
              case 0x00:
              case 0x01:
              case 0x02:
                if (msg.data[1] == RELAY_0_ID) {
                  switch_exec(msg.data[0], false);
                }
                break;
              case 0x03:
                switch_exec((msg.data[1] >> (RELAY_0_ID - 1)) & 0x01, false);
                break;
            }
            switchResponse(msg.src_address);
          }
        }
        break;
    }
  }

  ArduinoOTA.handle();
  yield();
}
