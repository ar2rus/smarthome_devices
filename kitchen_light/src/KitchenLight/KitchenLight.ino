/**
    Use 2.6.2 esp8266 core
    lwip 2 Higher bandwidth; CPU 80 MHz
    128K SPIFFS

     dependencies:
      https://github.com/PaulStoffregen/Time
      https://github.com/gmag11/NtpClient
      https://github.com/gmag11/FSBrowserNG

      https://github.com/me-no-dev/ESPAsyncUDP
      https://github.com/me-no-dev/ESPAsyncWebServer
      https://github.com/arthurgargon/ClunetMulticast

 */


#include <FS.h>
#include <FSWebServerLib.h>
#include <ESPAsyncWebServer.h>

#include <TimeLib.h>

#include <ClunetMulticast.h>

AsyncWebServer server(8080);
ClunetMulticast clunet(0x82, "KitchenLight");

const char RELAY_0_ID = 1;
const char BUTTON_ID = 3;

const int BUTTON_PIN = 12;
const int LIGHT_PIN = 14;

int button_state;
int light_state = LOW;

const int pwmrange = 255;
int dimmer_value = 0;

//fade-in
unsigned long fade_in_start_time = 0;

void setup() {
  pinMode(BUTTON_PIN, INPUT);

  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, !light_state);

  button_state = digitalRead(BUTTON_PIN);

  analogWriteRange(pwmrange);
  analogWriteFreq(100);


  Serial.begin(115200);
  Serial.println("Booting");
  
  SPIFFS.begin();
  ESPHTTPServer.begin(&SPIFFS);

    if (clunet.connect()){
      clunet.onMessage([](clunet_message* msg){
         switch (msg->command) {
            case CLUNET_COMMAND_SWITCH:
              if (msg->data[0] == 0xFF) { //info request
                if (msg->size == 1) {
                  switchResponse(msg->src_address);
                }
              } else {
                if (msg->size == 2) {
                  switch (msg->data[0]) {
                    case 0x00:
                    case 0x01:
                    case 0x02:
                      if (msg->data[1] == RELAY_0_ID) {
                        switch_exec(msg->data[0], false);
                      }
                      break;
                    case 0x03:
                      switch_exec((msg->data[1] >> (RELAY_0_ID - 1)) & 0x01, false);
                      break;
                  }
                  switchResponse(msg->src_address);
                }
              }
              break;
            case CLUNET_COMMAND_BUTTON:
              if (msg->size == 0) {
                buttonResponse(msg->src_address);
              }
              break;
            case CLUNET_COMMAND_DIMMER:
              if (msg->size == 1 && msg->data[0] == 0xFF) {
                dimmerResponse(msg->src_address);
              } else if (msg->size == 2) {
                //у нас только один канал. Проверяем, что команда для него
                if ((msg->data[0] >> (RELAY_0_ID - 1)) & 0x01) {
                  dimmer_exec(msg->data[1], false);
                  dimmerResponse(msg->src_address);
                }
              }
          }
      });
    }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
    int r = 404;
    if (request->args() == 0) {
      if (switch_toggle(true)){
        r = 200;
      }
    }
    server_response(request, r);
  });

  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){  //on and dimmer
    int r = 404;
      switch (request->args()) {
        case 0:
          if (switch_on(true)){
            r = 200;
          }
          break;
        case 1:
          if(request->hasArg("d")){//dimmer: 0 - 255
            String arg = request->arg("d");
            
            //check digits in arg value
            byte num_digits = 0;
            for (byte i = 0; i < arg.length(); i++) {
              if (isDigit(arg.charAt(i))) {
                num_digits++;
              }else{
                num_digits = 0;
                break;
              }
            }

            r = 400;
            if (num_digits && num_digits <= 3) {  //0-255, maximum 3 digits
              if (dimmer_exec(arg.toInt(), true)) {
                r = 200;
              }
            }
          }
          break;
      }
    server_response(request, r);
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){  //off
    int r = 404;
    if (request->args() == 0) {
      if (switch_off(true)){
        r = 200;
      }
    }
    server_response(request, r);
  });

  server.on("/fadein", HTTP_GET, [](AsyncWebServerRequest *request){ //fade-in
    int r = 404;
    if (request->args() == 0) {
      if (fade_in_start()){
        r = 200;
      }
    }
    server_response(request, r);
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

   server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) {
    ESP.restart();
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
    case 400:
      request->send(400, "text/plain", "Bad request\n\n");
      break;
    default:
      //case 404:
      request->send(404, "text/plain", "File Not Found\n\n");
      break;
  }
}

void switchResponse(unsigned char address) {
  char info = (light_state << (RELAY_0_ID - 1));
  clunet.send(address, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

bool switchExecute(byte command) {
  switch (command) {
    case 0x00:  //откл
      light_state = LOW;
      break;
    case 0x01: //вкл
      light_state = HIGH;
      break;
    case 0x02: //перекл
      light_state = !light_state;
      break;
    default:
      return false;
  }

  //disable pwm
  dimmer_value = 0;
  analogWrite(LIGHT_PIN, dimmer_value);
  //set value
  digitalWrite(LIGHT_PIN, !light_state);
  return true;
}

bool switch_exec(byte command, bool send_response) {
  bool r = switchExecute(command);
  if (r) {
    if (send_response) {
      switchResponse(CLUNET_BROADCAST_ADDRESS);
    }
    fade_in_stop(false);
  }
  return r;
}

bool switch_on(bool send_response) {
  return switch_exec(0x01, send_response);
}

bool switch_off(bool send_response) {
  return switch_exec(0x00, send_response);
}

bool switch_toggle(bool send_response) {
  return switch_exec(0x02, send_response);
}

void dimmerResponse(unsigned char address) {
  char data[] = {1, RELAY_0_ID, dimmer_value};
  clunet.send(address, CLUNET_COMMAND_DIMMER_INFO, data, sizeof(data));
}

bool dimmerExecute(int value) {
  if (value >= 0 && value <= pwmrange) {
    dimmer_value = value;
    light_state = value > 0;
    analogWrite(LIGHT_PIN, pwmrange - dimmer_value);
    return true;
  }
  return false;
}

bool dimmer_exec(int value, bool send_response) {
  bool r = dimmerExecute(value);
  if (r && send_response) {
    dimmerResponse(CLUNET_BROADCAST_ADDRESS);
  }
  return r;
}

bool fade_in_start() {
  if (!fade_in_start_time) {
    fade_in_start_time = millis();
    return true;
  }
  return false;
}

bool fade_in_stop(bool send_response) {
  if (fade_in_start_time) {
    fade_in_start_time = 0;
    if (send_response) {
      dimmerResponse(CLUNET_BROADCAST_ADDRESS);
    }
    return true;
  }
  return false;
}

void buttonResponse(unsigned char address) {
  char data[] = {BUTTON_ID, !button_state};
  clunet.send(address, CLUNET_COMMAND_BUTTON_INFO, data, sizeof(data));
}

unsigned long button_pressed_time = 0;

const int delay_before_toggle = 25;
const int delay_before_pwm = 500;
const int pwm_down_up_cycle_time = 4000;
const int pwm_down_up_cycle_time_2 = pwm_down_up_cycle_time / 2;

void loop() {
  int button_tmp = digitalRead(BUTTON_PIN);
  unsigned long m = millis();

  if (button_state != button_tmp) {
    if (button_tmp == LOW) { //pressed

      if (!button_pressed_time) {
        button_pressed_time = m;
      }
      if (m - button_pressed_time >= delay_before_toggle) { //HIGH->LOW
        button_state = button_tmp;  //LOW
        buttonResponse(CLUNET_BROADCAST_ADDRESS);
        switch_toggle(true);
        if (!light_state) { //погасили
          button_pressed_time = 0;  //таймер и диммер не нужны
        }
      }
    } else { //LOW->HIGH
      button_state = button_tmp; //HIGH
      buttonResponse(CLUNET_BROADCAST_ADDRESS);

      if (button_pressed_time) {
        fade_in_stop(true);
      }
    }
  }

  if (button_tmp == HIGH) {
    button_pressed_time = 0;
  }

  if (button_pressed_time) {
    if (m - button_pressed_time >= delay_before_pwm) {
      fade_in_start();
    }
  }

  //fade_in_update
  if (fade_in_start_time) {
    int v0 = (m - fade_in_start_time) % pwm_down_up_cycle_time;
    int v1 = v0 % pwm_down_up_cycle_time_2;
    if (v0 >= pwm_down_up_cycle_time_2) { //up
      dimmer_exec(pwmrange * v1 / (pwm_down_up_cycle_time_2 - 1), false);
    } else { //down
      dimmer_exec(pwmrange - pwmrange * v1 / (pwm_down_up_cycle_time_2 - 1), false);
    }
  }

  ESPHTTPServer.handle();
  yield();
}
