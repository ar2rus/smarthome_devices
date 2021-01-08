/**
 * Use 2.6.3 esp8266 core
 * lwip 1.4 Higher bandwidth; CPU 80 MHz
 * 1M (64K) !!!
 * 
 * dependencies:
 * ESPAsyncWebServer https://github.com/me-no-dev/ESPAsyncWebServer
 * ESPMiio https://github.com/ar2rus/ESPMiIO
 * ESPInputs https://github.com/ar2rus/ESPInputs
 * PTTasker https://github.com/ar2rus/PTTasker
 * ClunetMulticast https://github.com/ar2rus/ClunetMulticast
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <Servo.h>

#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include <ESPInputs.h>
#include <ESPMiio.h>
#include <ESPVacuum.h>

#include <ptt.h>

#include "Socle.h"
#include "Credentials.h"

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 125);     //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer server(80);

Servo servo;

PTTasker tasker;
Inputs inputs;

IPAddress mirobo_ip(192, 168, 1, 27);
MiioDevice mirobo(&mirobo_ip, MIROBO_TOKEN, 2000);

int mirobo_state = VS_UNKNOWN;

ClunetMulticast clunet(CLUNET_ID, CLUNET_DEVICE);


void servo_attach(){
  servo.attach(SERVO_PIN);
  digitalWrite(LED_BLUE_PORT, HIGH);
}

void servo_detach(){
  servo.detach();
  digitalWrite(LED_BLUE_PORT, LOW);
}

void stop_servo_group(){
	tasker.stopAll(TASKER_GROUP_SERVO);
	tone(BUZZER_PORT, 0);
}

PT_THREAD(beep(pt_t *p, int freq, int tone_delay, int pause_delay)){
    PT_BEGIN(p);
    tone (BUZZER_PORT, freq);
    PT_DELAY(p, tone_delay);
    tone (BUZZER_PORT, 0);
    PT_DELAY(p, pause_delay);
    PT_END(p);
}

PT_THREAD(beep_warning(pt_t *p)){
    PT_BEGIN(p);
    PT_SUBTHREAD_R(p, beep, BEEP_WARNING_REPEATS, BEEP_WARNING_FREQ, BEEP_WARNING_DELAY, BEEP_WARNING_DELAY);
    PT_END(p);
}

PT_THREAD(beep_error(pt_t *p)){
    PT_BEGIN(p);
    PT_SUBTHREAD(p, beep, BEEP_ERROR_FREQ, BEEP_ERROR_DELAY, 0);
    PT_END(p);
}

PT_THREAD(beep_info(pt_t *p)){
    PT_BEGIN(p);
    PT_SUBTHREAD_R(p, beep, 1, BEEP_INFO_FREQ, BEEP_INFO_DELAY, 0);
    PT_END(p);
}

void beep_error(){
  tasker.once(&beep_error);
}

PT_THREAD(beep_app_start(pt_t *p)){
    PT_BEGIN(p);
    PT_SUBTHREAD(p, beep, BEEP_APP_START_FREQ, BEEP_APP_START_DELAY, 0);
    PT_END(p);
}

void stop_group_led_red(){
  tasker.stopAll(TASKER_GROUP_LED_RED);
  digitalWrite(LED_RED_PORT, LOW);
}

PT_THREAD(led_red_blink(pt_t *p, int delay_high, int delay_low)){
    PT_BEGIN(p);
    digitalWrite(LED_RED_PORT, HIGH);
    PT_DELAY(p, delay_high);
    digitalWrite(LED_RED_PORT, LOW);
    PT_DELAY(p, delay_low);
    PT_END(p);
}

PT_THREAD(led_red_ok(pt_t *p)){
    PT_BEGIN(p);
    PT_SUBTHREAD(p, led_red_blink, 100, 900);
    PT_END(p);
}

void led_red_ok(){
  stop_group_led_red();
  tasker.loop(TASKER_GROUP_LED_RED, &led_red_ok);
}

PT_THREAD(led_red_error_1(pt_t *p)){
    PT_BEGIN(p);
    PT_SUBTHREAD(p, led_red_blink, 250, 250);
    PT_END(p);
}

void led_red_error_1(){
  stop_group_led_red();
  tasker.loop(TASKER_GROUP_LED_RED, &led_red_error_1);
}

PT_THREAD(led_red_error_2(pt_t *p)){
    PT_BEGIN(p);
    PT_SUBTHREAD(p, led_red_blink, 500, 500);
    PT_END(p);
}

void led_red_error_2(){
  stop_group_led_red();
  tasker.loop(TASKER_GROUP_LED_RED, &led_red_error_2);
}

PT_THREAD(servo_angle(pt_t *p, int angle)){
  int val = servo.read();
  PT_BEGIN(p);
  servo.write(val);
  servo_attach();
  while (val != angle){
    servo.write(val + (angle > val ? 1 : -1));
    PT_DELAY(p, SERVO_STEP_DELAY);
    val = servo.read();
  }  
  clunet.send(CLUNET_ADDRESS_BROADCAST, CLUNET_COMMAND_SERVO_INFO, (char*)&angle, 2);
  PT_END(p);
}

PT_THREAD(servo_down(pt_t *p)){
  PT_BEGIN(p);
  //если не в нижнем положении
  if (servo.read() != SERVO_DOWN_ANGLE){
    //предупреждение
    PT_SUBTHREAD(p, beep_warning);
    //опускаем
    PT_SUBTHREAD(p, servo_angle, SERVO_DOWN_ANGLE);
    //ждем немного после опускания
    PT_DELAY(p, SERVO_DOWN_TIMEOUT_DEFAULT);
  }
  //выключаем серву
  servo_detach();
  PT_END(p);
}

bool servo_down(bool force){
  if (force){
    stop_servo_group();
  }
  return tasker.once(TASKER_GROUP_SERVO, &servo_down);
}

uint32_t servo_up_timeout;

PT_THREAD(servo_up(pt_t *p)){
  PT_BEGIN(p);
  //если не в верхнем положении
  if (servo.read() != SERVO_UP_ANGLE){
    //предупреждение
    PT_SUBTHREAD(p, beep_warning);
    //поднимаем
    PT_SUBTHREAD(p, servo_angle, SERVO_UP_ANGLE);
  }
  //в верхнем положении находимся не более timeout
  PT_WAIT(p, servo.read() != SERVO_UP_ANGLE, servo_up_timeout);
  //если все еще в верхнем положении
  if (servo.read() == SERVO_UP_ANGLE){
     //опускаем вниз
     PT_SUBTHREAD(p, servo_down);
  }
  PT_END(p);
}

bool servo_up(bool force, uint32_t timeout = SERVO_UP_TIMEOUT_DEFAULT){
  if (force){
    stop_servo_group();
  }
  servo_up_timeout = timeout;
  return tasker.once(TASKER_GROUP_SERVO, &servo_up);
}

bool servo_toggle(bool force){
  //если куда-то двигаемся(не в крайних положениях), то переключиться нельзя
  switch (servo.read()){
    case SERVO_UP_ANGLE:
      return servo_down(force);
    case SERVO_DOWN_ANGLE:
      return servo_up(force);
    default:
      return false; 
  }
}

PT_THREAD(mirobo_connect(pt_t *p)){
    PT_BEGIN(p);
    if (!mirobo.isConnected()){
      mirobo.connect([](MiioError e){
        led_red_error_1();
        Serial.printf("PT connecting error: %d\n", e);
      });
    }
    PT_WAIT_UNTIL(p, !mirobo.isBusy());
    if (mirobo.isConnected()){
      led_red_ok();
    }
    PT_END(p);
}

PT_THREAD(mirobo_toggle(pt_t *p)){
  PT_BEGIN(p);
  PT_SUBTHREAD(p, beep_app_start);
  PT_SUBTHREAD(p, mirobo_connect);

  if (mirobo.isConnected()){
    if (mirobo_state == VS_CHARGING){
      mirobo.send("app_start", NULL,  [](MiioError e){
            beep_error();
            Serial.printf("PT app_start error: %d\n", e);
      });
    }else{
      mirobo.send("app_pause", NULL,  [](MiioError e){
          beep_error();    
          Serial.printf("PT app_pause error: %d\n", e);
      });
      PT_WAIT_UNTIL(p, !mirobo.isBusy());
      mirobo.send("app_charge", NULL,  [](MiioError e){
            beep_error();
            Serial.printf("PT app_charge error: %d\n", e);
      });
    }
  }else{
      beep_error();
      Serial.printf("PT error: mirobo not connected");
  }
  PT_END(p);
}

PT_THREAD(check_mirobo_status(pt_t *p)){
    PT_BEGIN(p);
    PT_SUBTHREAD(p, mirobo_connect);
    
    if (!mirobo.send("get_status", [](MiioResponse response){
          if (!response.getResult().isNull()){
            JsonVariant state = response.getResult()["state"];
            if (!state.isNull()){
              led_red_ok();
              int s = state.as<int>();
              Serial.printf("state=%d\n", s);
              if (s != mirobo_state){
                switch (s){
                 case VS_GOING_HOME:
                  servo_up(true, MIROBO_MAX_GOING_HOME_PERIOD);
                  break;
                 case VS_CHARGING:
                  servo_down(true);
                  break;
                 default:
                  if (mirobo_state == VS_CHARGING){
                    switch (s){
                      case VS_CLEANING:
                      case VS_MANUAL:
                      case VS_SPOT_CLEANUP:
                      case VS_GOING_TO_TARGET:
                      case VS_CLEANING_ZONE:
                        servo_up(true, 45000);
                    }
                  }else if (mirobo_state == VS_GOING_HOME){
                    switch (s){
                      case VS_REMOTE_CONTROL:
                      case VS_CLEANING:
                      case VS_MANUAL:
                      case VS_SPOT_CLEANUP:
                      case VS_SHUTDOWN:
                      case VS_GOING_TO_TARGET:
                      case VS_CLEANING_ZONE:
                      case VS_PAUSED:
                        servo_down(true);
                    }
                  }
                }
                mirobo_state = s;
                send_clunet_device_state_info(CLUNET_ADDRESS_BROADCAST);
              }
            }
          }else{
            if (!response.getError().isNull()){
              led_red_error_2();
              mirobo.disconnect();
              Serial.println("PT response error");
            }
          }
        }, [](MiioError e){
          led_red_error_2();
          mirobo.disconnect();
          Serial.printf("PT get_status error: %d\n", e);
        }
    )){
      led_red_error_2();
      mirobo.disconnect();
      Serial.printf("PT send get_status error");
    }
    //Serial.println(ESP.getFreeHeap());
    PT_DELAY(p, MIROBO_CHECK_STATUS_PERIOD);
    PT_END(p);
}

void send_clunet_device_state_info(uint8_t address){
  char data[] = {3, *(char*)&mirobo_state};
  clunet.send(address, CLUNET_COMMAND_DEVICE_STATE_INFO, data, sizeof(data));
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(1000);
    ESP.restart();
  }

  Serial.println("Connetcted");

  ArduinoOTA.setHostname("swing-socle");

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


  server.on("/up", HTTP_GET, [](AsyncWebServerRequest * request) {
    char temp[16];
    sprintf(temp, "{\"result\": %u}", servo_up(false));
    request->send(200, "application/json", temp);
  });

  server.on("/down", HTTP_GET, [](AsyncWebServerRequest * request) {   
     char temp[16];
     sprintf(temp, "{\"result\": %u}", servo_down(false));
     request->send(200, "application/json", temp);
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest * request) {
    char temp[16];
    sprintf(temp, "{\"result\": %u}", servo_toggle(false));
    request->send(200, "application/json", temp);
  });

  server.on("/mirobo_state", HTTP_GET, [](AsyncWebServerRequest * request) {
    char temp[16];
    sprintf(temp, "{\"state\": %u}", mirobo_state);
    request->send(200, "application/json", temp);
  });

  server.on("/mirobo_start", HTTP_GET, [](AsyncWebServerRequest * request) {
    tasker.once(TASKER_GROUP_MIIO, &mirobo_toggle);
    request->send(200);
  });

//   server.on("/hello", HTTP_GET, [](AsyncWebServerRequest * request) {
//      if (mirobo.connect([](MiioError e){
//          Serial.printf("hello error: %d", e);
//        })){
//        request->send(200);
//        return;
//      }
//
//       request->send(500);
//   });
//
//   server.on("/status", HTTP_GET, [](AsyncWebServerRequest * request) {
//      if (mirobo.isConnected()){
//        if (mirobo.send("get_status", 
//        [](Response response){
//          Serial.println("MIROBO status received");
//        }, 
//        [](MiioError e){
//          Serial.printf("MIROBO get_status error: %d\n", e);
//        })){
//          request->send(200);
//          return;
//        }
//      }else{
//        request->send(403);
//      }
//      
//      if (mirobo.send("get_status", [](Response response){
//          Serial.println("MIROBO response received");
//        })){
//        request->send(200);
//        return;
//      }
//       request->send(500);
//   });

  uint16_t id_t0 = inputs.on(0, STATE_CHANGE, 10, [](uint8_t state){
    Serial.println("task0");
    digitalWrite(LED_RED_PORT, !state);
  });

  uint16_t id_t1 = inputs.on(0, STATE_LOW, 5000, [id_t0](uint8_t state){
    Serial.println("removing task");
    inputs.remove(id_t0);
  });
  
  pinMode(LED_RED_PORT, OUTPUT);
  pinMode(LED_BLUE_PORT, OUTPUT);
  pinMode(BUZZER_PORT, OUTPUT);  

  servo.write(SERVO_DOWN_ANGLE);

  tasker.loop(TASKER_GROUP_MIIO, &check_mirobo_status);

  server.begin();

  if (clunet.connect()){
    Serial.println("connected");
    clunet.onPacketReceived([](clunet_packet* packet){
      
    Serial.printf("message received: %d\n", packet->command);
      switch (packet->command) {
        case CLUNET_COMMAND_DEVICE_STATE: {
            if (packet->size == 1) {
              switch (packet->data[0]){
                case 0x01:{
                  tasker.once(TASKER_GROUP_MIIO, &mirobo_toggle);
                  break;
                }
                case 0xFF:{
                  send_clunet_device_state_info(packet->src);
                  break;
                }
              }
            }
            break;
        }
        case CLUNET_COMMAND_BEEP:{
          tasker.once(&beep_info);
          break;
        }
        //mirobo_toggle by long press on kitchen button
        case CLUNET_COMMAND_BUTTON_INFO: {
          if (packet->src == 0x1D){
            if (packet->size == 2 && packet->data[0] == 02 && packet->data[1] == 01){
              tasker.once(TASKER_GROUP_MIIO, &mirobo_toggle);
            }
          }
          break;
        }
      }
    });
  }
  
  
}

void loop() {
  tasker.handle();
  inputs.handle();
  
  ArduinoOTA.handle();
  yield();
}
