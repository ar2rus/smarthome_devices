#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "ClunetMulticast.h"

const char *ssid = "espNet";
const char *pass = "esp8266A";

IPAddress ip(192,168,1,122);  //Node static IP
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);


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

const int delay_before_pwm = 500;
const int pwm_down_up_cycle_time = 4000;
const int pwm_down_up_cycle_time_2 = pwm_down_up_cycle_time / 2;

void loop() {
  int button_tmp = digitalRead(BUTTON_PIN);
  if (button_state != button_tmp){
    button_state = button_tmp;
    buttonResponse(CLUNET_BROADCAST_ADDRESS);
    if (button_state == LOW){
      switchExecute(0x02);
      if (light_state){
        button_pressed_time = millis();
      }
      switchResponse(CLUNET_BROADCAST_ADDRESS);
    }else{
      if (button_pressed_time > delay_before_pwm){
        dimmerResponse(CLUNET_BROADCAST_ADDRESS);
      }
      button_pressed_time = 0;
    }
    delay(5);  //дребезг
  }

  if (button_pressed_time){
    unsigned long m = millis();
    if (m - button_pressed_time > delay_before_pwm){
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
  
  ArduinoOTA.handle();
}
