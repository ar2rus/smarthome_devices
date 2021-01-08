/**
 * Use 2.6.3 esp8266 core
 * lwip 1.4 Higher bandwidth; CPU 80 MHz
 * 1M (FS: 128K) !!!
 * 
 * dependencies:
 * ESPAsyncWebServer https://github.com/me-no-dev/ESPAsyncWebServer
 * PTTasker https://github.com/ar2rus/PTTasker
 * ClunetMulticast https://github.com/ar2rus/ClunetMulticast
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <Servo.h>

//#include "ESPAsyncTCP.h"
#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include "WaterSystem.h"
#include "Credentials.h"


const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 123); //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer server(80);

ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);

Servo servo;
int pump_state = LOW;

TaskExt task_queue[TASK_QUEUE_MAX_LENGTH];
volatile int task_queue_count = 0;

Task task_queue_tmp[TASK_QUEUE_MAX_LENGTH];

long fill_time[POT_COUNT];

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
 
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, pump_state);
  
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

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
  //  char r = 404;
  //  if (request->args() == 0) {
  //    if (switch_toggle(true)){
  //      r = 200;
  //    }
  //  }
  //  server_response(request, r);

   //request->send(200, "text/plain", String(task_queue_count) + " : " + String(task_queue[0].id) + ":" + String(task_queue[0].start_time) + ":" + String(task_queue[0].param) + ":" + String(fill_time[2]));
   //request->send(SPIFFS, "/index.html");

   request->send_P(200, "text/html", index_html);
   
  });

  server.on("/water", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
    int r = 404;
    int n = 0;
    
    if (request->args() == 1) {
      if(request->hasArg("pot")){
        String arg = request->arg("pot");
        if (checkUintArg(arg)) {
          n = set_task(get_water_task(task_queue_tmp, fill_time, arg.toInt()));
        }
      }else if (request->hasArg("all")){
        n = set_task(get_water_all_task(task_queue_tmp, fill_time));
      }
    }

    if (n > 0){
      r = 200;
    }else if (n < 0){
      r = 403; //запрещенное действие 
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
            if (set_task(get_servo_task(task_queue_tmp, pot))){
              r = 200;
            }
          }
      }else if (request->hasArg("test")){
          if (set_task(get_servo_test_task(task_queue_tmp, 10, 1000))){
             r = 200;
          }
      }
    }
    server_response(request, r);
  });

  server.on("/pump_on", HTTP_GET, [](AsyncWebServerRequest *request){
    int r = 404;
    if (request->args() == 0) {
          if (set_task(get_pump_task(task_queue_tmp, 1))){
             r = 200;
          }
    }
    server_response(request, r);
  });

  server.on("/pump_off", HTTP_GET, [](AsyncWebServerRequest *request){
    int r = 404;
    if (request->args() == 0) {
          if (set_task(get_pump_task(task_queue_tmp, 0))){
             r = 200;
          }
    }
    server_response(request, r);
  });

  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request){
    reset_task_queue();
    server_response(request, 200);
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.onNotFound( [](AsyncWebServerRequest *request) {
    server_response(request, 404);
  });

  server.begin();

  if (clunet.connect()){
    Serial.println("connected");
    clunet.onPacketReceived([](clunet_packet* packet){
      Serial.printf("message received: %d\n", packet->command);
      switch (packet->command) {
        case CLUNET_COMMAND_SWITCH:{
          if (packet->data[0] == 0xFF) { //info request
            if (packet->size == 1) {
              pumpResponse(packet->src);
            }
          }
        }
        break;
        case CLUNET_COMMAND_SERVO:{
          if (packet->data[0] == 0xFF) { //info request
            if (packet->size == 1) {
              servoResponse(packet->src, servo.read());
            }
          }
        }
        break;
      }
    });
  }
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
      request->send(403, "text/plain", "Too frequent request\n\n");
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
  clunet.send(address, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

void servoResponse(unsigned char address, int16_t angle){
  clunet.send(address, CLUNET_COMMAND_SERVO_INFO, (char*)&angle, sizeof(angle));
}

boolean pump_exec(char command) {
  boolean send_response = pump_state != command;
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
  
  if (send_response) {
    pumpResponse(CLUNET_ADDRESS_BROADCAST);
  }
  return true;
}

void servo_exec(int angle){
  servo.attach(SERVO_PIN);
  servo.write(angle);
  servoResponse(CLUNET_ADDRESS_BROADCAST, angle);
}

void stop_all(){
  pump_exec(0);
  servo.detach();
}

void copy_task(TaskExt* dst, Task* src){
  dst->id = src->id;
  dst->param = src->param;
  dst->start_time = 0;
}

void reset_task_queue(){
  stop_all();
  task_queue_count = 0;
}

int set_task(int n){
  if (n > 0){
    reset_task_queue();
    
    for (int i=0; i<n; i++){
      copy_task(&task_queue[i], &task_queue_tmp[i]);
    }
    task_queue_count = n;
  }
  return n;
}

void update_task_queue(){
  if (task_queue_count > 0){
    
    boolean interrupt = false;  //прервать выполнение очереди задач
    boolean next = true;        //перейти к слудующему элементу очереди
    
    long t = millis();
    
    TaskExt* task = &task_queue[0];
    if (task->start_time == 0){
      task->start_time = t;   //save starttime for a new task
    }
    
    switch (task->id){
      case TASK_SERVO:
        servo_exec(task->param);
        break;
      case TASK_SERVO_DETACH:
        servo.detach();
        break;
      case TASK_PUMP:
        pump_exec(task->param);
        break;
      case TASK_DELAY:
        next = (t - task->start_time) >= task->param;
        break;
      case TASK_WATER_TIME_CHECK:
        interrupt = !can_water_pot(fill_time, task->param);
        break;
      case TASK_WATER_TIME_SAVE:
        fill_time[task->param] = t;
        break;
    }

    if (interrupt){
      reset_task_queue();
    }else if (next){
      task_queue_count--;
      //shift queue
      for (int i=0; i<task_queue_count; i++){
        copy_task(&task_queue[i], &task_queue[i+1]);
      }
    }
  }
}

void loop() {
  update_task_queue();

  ArduinoOTA.handle();
  yield();
}
