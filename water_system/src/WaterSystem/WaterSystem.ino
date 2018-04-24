#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <Servo.h>
#include "WaterSystem.h"

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


Servo servo;
int pump_state = LOW;

long fill_time[POT_COUNT] = {0};


void setup() {
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, pump_state);
  
  servo.attach(SERVO_PIN);

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

  server.on("/water", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
    int r = 404;
    if (request->args() == 1) {
      if(request->hasArg("pot")){
          String arg = request->arg("pot");

          if (checkUintArg(arg)) {
            int pot = arg.toInt();
            if (has_pot(pot)){
              if (pot_exec(pot)) {
                r = 200;
              }else{
                r = 403;  //горшок есть, но поливать еще пока нельзя -> ждем пока утечет вода
              }
            }
          }
       }else if (request->hasArg("all")){
          if (pot_exec_all()){
             r = 200;
          }
        }
    }
    server_response(request, r);
  });

  server.on("/servo", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
    int r = 404;
    if (request->args() == 1) {
      if(request->hasArg("pot")){
          String arg = request->arg("pot");
  
          if (checkUintArg(arg)) {
            int pot = arg.toInt();
            if (pot_pos(pot)){
                r = 200;
              }
          }
      }else if (request->hasArg("test")){
          for (int i=0; i<20; i++){
            pot_pos(random(POT_COUNT));
            delay(1000);
          }
      }
    }
    server_response(request, r);
  });

  server.on("/pump_on", HTTP_GET, [](AsyncWebServerRequest *request){  //on and dimmer
    int r = 404;
      switch (request->args()) {
        case 0:
          if (pump_on(true)){
            r = 200;
          }
          break;
      }
    server_response(request, r);
  });

  server.on("/pump_off", HTTP_GET, [](AsyncWebServerRequest *request){  //off
    int r = 404;
    if (request->args() == 0) {
      if (pump_off(true)){
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

boolean checkUintArg(String argument){
   for (byte i = 0; i < argument.length(); i++) {
      if (!isDigit(argument.charAt(i))) {
        return false;
      }
   }
   return argument.length() > 0;
}

void server_response(AsyncWebServerRequest *request, unsigned int response) {
  switch (response) {
    case 200:
      request->send(200);
      break;
    case 403:
      request->send(403, "text/plain", "Too frequent period\n\n");
      break;
    default:
      //case 404:
      request->send(404, "text/plain", "File Not Found\n\n");
      break;
  }
}


const char RELAY_0_ID = 1;

void pumpResponse(unsigned char address) {
  char info = (pump_state << (RELAY_0_ID - 1));
  clunetMulticastSend(address, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

boolean pumpExecute(char command) {
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

boolean pump_exec(char command, boolean send_response) {
  boolean r = pumpExecute(command);
  if (r) {
    if (send_response) {
      pumpResponse(CLUNET_BROADCAST_ADDRESS);
    }
  }
  return r;
}

boolean pump_on(boolean send_response) {
  return pump_exec(0x01, send_response);
}

boolean pump_off(boolean send_response) {
  return pump_exec(0x00, send_response);
}

boolean pump_toggle(boolean send_response) {
  return pump_exec(0x02, send_response);
}

boolean has_pot(int i){
  return i >=0 && i<POT_COUNT;
}

boolean pot_pos(int i){
  if (has_pot(i)){
    servo.write(pot_angle[i]);
    delay(100); //TODO: killme
    return true;
  }
  return false;
}

boolean can_pot_fill(int i){
  return  has_pot(i) && ((fill_time[i] == 0) || (millis() - fill_time[i] > POT_FILL_PERIOD));
}

boolean pot_fill(int i){
  if (can_pot_fill(i)){
    
    pump_on(i);
    delay(POT_FILL_TIME);
    pump_off(i);
    fill_time[i] = millis();
    
    return true;
  }
  return false;
}

boolean pot_exec(int i){
  return can_pot_fill(i) && pot_pos(i) && pot_fill(i);
}

boolean pot_exec_all(){
  for (int i=0; i<POT_COUNT; i++){
    pot_exec(i);
  }
  
    return true;
}



void loop() {
 clunet_msg msg;
  if (clunetMulticastHandleMessages(&msg)) {
    switch (msg.command) {
      case CLUNET_COMMAND_SWITCH:
        if (msg.data[0] == 0xFF) { //info request
          if (msg.size == 1) {
            pumpResponse(msg.src_address);
          }
        }
        break;
    }
  }

  ArduinoOTA.handle();
  yield();
}
