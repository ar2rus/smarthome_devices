#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <ESP8266WebServer.h>

#include "ClunetMulticast.h"

const char *ssid = "espNet";
const char *pass = "esp8266A";

IPAddress ip(192,168,1,122);  //Node static IP
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

ESP8266WebServer server(80);


const int BUTTON_PIN = 12;
const int LIGHT_PIN = 14;

int button_state;
int light_state = LOW;

//dimmer
const char pwmrange = 100;  //0 - 100
int dimmer_value = 0;

void setup() {
  pinMode(BUTTON_PIN, INPUT);
  
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, !light_state);

  button_state = digitalRead(BUTTON_PIN);

  analogWriteRange(pwmrange);
  analogWriteFreq(100);
  

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

  ArduinoOTA.setHostname("kitchen-light");

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

   server.on("/", []() {  //toggle
      char r = 404;
      if (server.method() == HTTP_GET && server.args()==0){
        switchExecute(0x02);
        switchResponse(CLUNET_BROADCAST_ADDRESS);
        r = 200;
      }
      server_response(r);
  });

   server.on("/on", []() {  //on and dimmer
      char r = 404;
      if (server.method() == HTTP_GET){
        switch (server.args()){
          case 0:
            switchExecute(0x01);
            switchResponse(CLUNET_BROADCAST_ADDRESS);
            r = 200;
            break;
          case 1:
            if (server.argName(0) == "d"){  //dimmer: 0 - 100
               //check digits in arg value
               char all_digits = 1;
               for(byte i=0;i<server.arg(0).length();i++){
                  if(!isDigit(server.arg(0).charAt(i))){
                    all_digits = 0;
                  }
               }

             if (all_digits){
               if (dimmerExecute(server.arg(0).toInt())){
                dimmerResponse(CLUNET_BROADCAST_ADDRESS);
                r = 200;
               }
             }
            }
            break;
        }
      }
      server_response(r);
  });

   server.on("/off", []() {  //off
      char r = 404;
      if (server.method() == HTTP_GET && server.args()==0){
        switchExecute(0x00);
        switchResponse(CLUNET_BROADCAST_ADDRESS);
        r = 200;
      }
      server_response(r);
  });
  
  server.onNotFound([]() {
      server_response(404);
  });
  
  server.begin();
}

void server_response(unsigned int response){
  switch (response){
    case 200:
      server.send(200, "text/plain", "OK\n\n");
    break;
    default:
    //case 404:
      server.send(404, "text/plain", "File Not Found\n\n");
    break;
  }
}


const char RELAY_0_ID = 1;

void switchResponse(unsigned char address){
  char info = (light_state << (RELAY_0_ID-1));
  clunetMulticastSend(address, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

void switchExecute(char command){
  switch(command){
    case 0x00:  //откл
      light_state = LOW;    
      break;
    case 0x01: //вкл
      light_state = HIGH;
      break;
    case 0x02: //перекл
      light_state = !light_state;
      break;
  }

  //disable pwm
  dimmer_value = 0;
  analogWrite(LIGHT_PIN, dimmer_value);

  //set value
  digitalWrite(LIGHT_PIN, !light_state);
}


char dimmerExecute(unsigned char value){
  if (value >= 0 && value <= pwmrange){
    dimmer_value = value;
    light_state = value > 0;
    analogWrite(LIGHT_PIN, pwmrange - dimmer_value);
    return 1;
  }
  return 0;
}

void dimmerResponse(unsigned char address){
  char data[] = {1, RELAY_0_ID, dimmer_value};
  clunetMulticastSend(address, CLUNET_COMMAND_DIMMER_INFO, data, sizeof(data));
}


const char BUTTON_ID = 3;

void buttonResponse(unsigned char address){
  char data[] = {BUTTON_ID, !button_state};
  clunetMulticastSend(address, CLUNET_COMMAND_BUTTON_INFO, data, sizeof(data));
}

unsigned long button_pressed_time = 0;

const int delay_before_toggle = 25;
const int delay_before_pwm = 500;
const int pwm_down_up_cycle_time = 4000;
const int pwm_down_up_cycle_time_2 = pwm_down_up_cycle_time / 2;

void loop() {
  
  int button_tmp = digitalRead(BUTTON_PIN);
  unsigned long m = millis();

  if (button_state != button_tmp){
    if (button_tmp == LOW){
      
      if (!button_pressed_time){
          button_pressed_time = m;
      }
      if (m - button_pressed_time >= delay_before_toggle){  //HIGH->LOW
          button_state = button_tmp;
          buttonResponse(CLUNET_BROADCAST_ADDRESS);
          switchExecute(0x02);
          switchResponse(CLUNET_BROADCAST_ADDRESS);
          if (!light_state){ //погасили
            button_pressed_time = 0;  //таймер и диммер не нужны
          }
      }
    }else{  //LOW->HIGH
       button_state = button_tmp;
       buttonResponse(CLUNET_BROADCAST_ADDRESS);
       
       if (button_pressed_time){
         if (m - button_pressed_time >= delay_before_pwm){
            dimmerResponse(CLUNET_BROADCAST_ADDRESS);
          }
      }
    }
  }

  if (button_tmp == HIGH){
      button_pressed_time = 0;
  }
  
  if (button_pressed_time){
    if (m - button_pressed_time >= delay_before_pwm){
      int v0 = (m - button_pressed_time - delay_before_pwm) % pwm_down_up_cycle_time;
      int v1 = v0 % pwm_down_up_cycle_time_2;
      if (v0 >= pwm_down_up_cycle_time_2){ //up
          dimmerExecute(100 * v1 / (pwm_down_up_cycle_time_2-1));
      }else{  //down
          dimmerExecute(100 - 100 * v1 / (pwm_down_up_cycle_time_2-1));
      }
    }
  }

    clunet_msg msg;
    if (clunetMulticastHandleMessages(&msg)){
      switch (msg.command){
        case CLUNET_COMMAND_SWITCH:
          if (msg.data[0] == 0xFF){ //info request
            if (msg.size == 1){
              switchResponse(msg.src_address);
            }
          }else{
            if (msg.size == 2){
              switch(msg.data[0]){
                case 0x00:
                case 0x01:
                case 0x02:
                  if (msg.data[1] == RELAY_0_ID){
                    switchExecute(msg.data[0]);
                  }
                  break;
                case 0x03:
                  switchExecute((msg.data[1] >> (RELAY_0_ID-1)) & 0x01);
                  break;
              }
              switchResponse(msg.src_address);
            }
          }
        break;
        case CLUNET_COMMAND_BUTTON:
          if (msg.size == 0){
            buttonResponse(msg.src_address);
          }
        break;
        case CLUNET_COMMAND_DIMMER:
          if (msg.size == 1 && msg.data[0] == 0xFF){
            dimmerResponse(msg.src_address);
          }else if (msg.size == 2){
            //у нас только один канал. Проверяем, что команда для него
            if ((msg.data[0] >> (RELAY_0_ID-1)) & 0x01){
              dimmerExecute(msg.data[1]);
              dimmerResponse(msg.src_address);
            }
          }
      }
    }

  server.handleClient();
  ArduinoOTA.handle();
}
