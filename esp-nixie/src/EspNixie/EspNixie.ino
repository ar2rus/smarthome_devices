/**
    Use 2.4.2 esp8266 core
    lwip 2 Higher bandwidth; CPU 160 MHz
    128K SPIFFS
  
    dependencies:
      https://github.com/PaulStoffregen/Time
      https://github.com/gmag11/NtpClient (origin/develop)
      https://github.com/gmag11/FSBrowserNG

      https://github.com/me-no-dev/ESPAsyncUDP
      https://github.com/me-no-dev/ESPAsyncWebServer
 * 
 */

#include <FS.h>
#include <FSWebServerLib.h>
#include <ESPAsyncWebServer.h>

#include <TimeLib.h>

#include "Narodmon.h"
#include "Nixie.h"
#include "Leds.h"
#include "InsideTermometer.h"
#include "Tasks.h"

#define ALARM_DURATION 15000
#define NUMBER_DURATION 5000

TaskWrapper* tw = new TaskWrapper();
Narodmon* nm = new Narodmon(WiFi.macAddress());
Leds* leds = new Leds();

AsyncWebServer server(8080);


//хранит параметр, переданный через метод /number
//его передача в callContinuousTask возможна только
//через глобальную переменную
uint32_t number_to_show;


/*void config_time(float timezone_hours_offset, int daylightOffset_sec,
                 const char* server1, const char* server2, const char* server3) {
  configTime((int)(timezone_hours_offset * 3600), daylightOffset_sec, server1, server2, server3);
  INFO("Timezone: %f; daylightOffset: %d", timezone_hours_offset, daylightOffset_sec);
}*/

void config_narodmon(String apiKey, uint8_t use_latlng, double lat, double lng, uint8_t radius) {
  nm->setApiKey(apiKey);
  if (use_latlng) {
    nm->setConfigLatLng(lat, lng);
  } else {
    nm->setConfigUseLatLng(0);
  }
  nm->setConfigRadius(radius);
}

void show_none() {
  nixie_clear();
  leds->backlight();
}

const CRGB MINUS_COLOR = CRGB::Blue;

void show_value(float v, uint8_t pos_1, int num_frac){
  nixie_set(v, pos_1, num_frac);

  if (v < 0) {  ///minus value
    leds->set([&](CRGB* leds, uint8_t leds_num, uint8_t* brightness){
 
        int lb = pos_1;
        float t = abs(v);
        while (((int)(t = t/10)) > 0){
          lb--;
        }
    
        lb = max(lb, 0);
        int rb = min(pos_1 + num_frac, leds_num - 1);
        for (int i=lb; i<=rb; i++){
          leds[i] = MINUS_COLOR;
        }
    });
  }else{
    leds->backlight();
  }

}

uint8_t available_clock() {
  return NTP.getLastNTPSync();
}

void _clock(){
    time_t t = now();
    int h = hour(t);
    int m = minute(t);
    int s = second(t);
    char p = t % 2;
    nixie_set(digit_code(h / 10, 1, 0), digit_code(h % 10, 1, p), digit_code(m / 10, 1, 0), digit_code(m % 10, 1, p), digit_code(s / 10, 1, 0), digit_code(s % 10, 1, p));
}

void show_clock() {
  _clock();
  leds->backlight();
}

/*
uint8_t available_clock_adjust() {
  return !available_clock();
}

void _clock_adjust(){
    time_t t = now();
    char p = (t / 500) % 6 ;    
    nixie_set(digit_code(0, p==0, 1), digit_code(0, p==1, 1), digit_code(0, p==2, 1), digit_code(0, p==3, 1), digit_code(0, p==4, 1), digit_code(0, p==5, 1));
}

void show_clock_adjust() {
  _clock_adjust();
  leds->backlight();
}
*/
uint8_t available_t() {
  return nm->hasT();
}

void show_t() {
  show_value(nm->getT(), 3, 1);
}

uint8_t available_p() {
  return nm->hasP();
}

void show_p() {
  show_value(nm->getP(), 3, 1);
}

uint8_t available_h() {
  return nm->hasH();
}

void show_h() {
  show_value(nm->getH(), 4, 0);
}

uint8_t available_t_inside() {
  return insideTermometerHasT();
}

void show_t_inside() {
  show_value(insideTermometerTemperature(), 2, 3);
}

void show_number() {
  show_value(number_to_show, 5, 0);
}

void config_modes(uint32_t clock_duration, 
  uint32_t t_duration, uint32_t p_duration, uint32_t h_duration,
  uint32_t t_inside_duration) {
  tw->reset();
  
  //tw->addContinuousTask(clock_duration, available_clock_adjust, show_clock_adjust);
  
  tw->addContinuousTask(clock_duration, available_clock, show_clock);
  tw->addContinuousTask(t_duration, available_t, show_t);
  if (p_duration){
    tw->addContinuousTask(clock_duration, available_clock, show_clock);
    tw->addContinuousTask(p_duration, available_p, show_p);
  }
  if (h_duration){
    tw->addContinuousTask(clock_duration, available_clock, show_clock);
    tw->addContinuousTask(h_duration, available_h, show_h);
  }
  if (t_inside_duration){
    tw->addContinuousTask(clock_duration, available_clock, show_clock);
    tw->addContinuousTask(t_inside_duration, available_t_inside, show_t_inside);
  }
  
  //  tw->addPeriodicalTask(1000, 60000, []() {
  //    insideTermometerRequest();
  //  });

  tw->addPeriodicalTask(10000, 120000, []() {
    nm->request();
  });
}


uint8_t ota_progress;

void setup() {
  DEBUG("Booting");
  nixie_init();

  SPIFFS.begin();
  ESPHTTPServer.begin(&SPIFFS);
    
  INFO("IP address: %s", WiFi.localIP().toString().c_str());

  //insideTermometerInit();

  //server.on("/", handleRoot);

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/narodmon", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", Logging._response);
  });

  server.on("/error", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", Logging._error);
  });

  server.on("/time", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", String(millis()));
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) {
    ESP.restart();
  });

  server.on("/clock", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(10000, available_clock, show_clock);
    request->send(200);
  });

  server.on("/t", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(10000, available_t, show_t);
    request->send(200);
  });

  server.on("/p", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(10000, available_p, show_p);
    request->send(200);
  });

  server.on("/h", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(10000, available_h, show_h);
    request->send(200);
  });

  server.on("/t_value", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", nm->hasT() ? String(nm->getT(), 1) : "");
  });

  server.on("/p", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(10000, available_p, show_p);
    request->send(200);
  });

  server.on("/p_value", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", nm->hasP() ? String(nm->getP(), 1) : "");
  });

  server.on("/h", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(10000, available_h, show_h);
    request->send(200);
  });

  server.on("/h_value", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", nm->hasH() ? String(nm->getH(), 1) : "");
  });

  server.on("/alarm", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(ALARM_DURATION, NULL, []() {
      _clock();
      leds->rainbow();
    });
    
    request->send(200);
  });

  server.on("/number", HTTP_GET, [](AsyncWebServerRequest *request) {
    int r = 404;
    if(request->hasArg("v")){ //отображает последние(младшие) 6 цифр переданного номера
      String v = request->arg("v");
      int length = _min(v.length(), 6);
      v = v.substring(_max(v.length() - length, 0));
      
      //check digits in arg value
      char all_digits = 1;
      for (byte i = 0; i < v.length(); i++) {
        if (!isDigit(v.charAt(i))) {
              all_digits = 0;
            }
      }
      
      if (all_digits) {
        number_to_show = v.toInt();
         tw->callContinuousTask(NUMBER_DURATION, NULL, []() {
            show_number();
         });
        r = 200;
      }
      
    }
    request->send(r);
  });

  server.on("/led", HTTP_GET, [](AsyncWebServerRequest * request) {
    leds->backlight_toggle();
    request->send(200);
  });

  server.onNotFound( [](AsyncWebServerRequest * request) {
    request->send(404, "text/plain", "File Not Found\n\n");
  });
  server.begin();


  ArduinoOTA.onStart([]() {
    ota_progress = 0;
    tw->callContinuousTask([]() {
      leds->set([&](CRGB* leds, uint8_t num_leds, uint8_t* brightness){
            const CRGB OTA_PROGRESS_COLOR = CRGB::Orange;
          
            for (int i = 0; i < num_leds; i++) {
            if (ota_progress >= (i + 1) * 100 / num_leds) {
              leds[i] = OTA_PROGRESS_COLOR;
            } else {
              break;
            }
          }
        });
      nixie_set(ota_progress, 5);
    });
  });

  ArduinoOTA.onEnd([]() {
    tw->callContinuousTask(show_none);
    tw->update();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    ota_progress = (progress / (total / 100));
    tw->update(); //main loop does not call while OTA
  });




  //config_time(4, 0, "pool.ntp.org", "time.nist.gov", NULL);
  config_narodmon("9M5UhuQA2c8f8", 1, 53.2266, 50.1915, 5);
  config_modes(10000, 5000, 5000, 0, 0);
  
  INFO("Setup done");
}

#define UPDATE_TIME 50

uint32_t update_t = 0;

void loop() {
  uint32_t t = millis();
  if (t - update_t > UPDATE_TIME) {
    update_t = t;
    tw->update();
  }

  ESPHTTPServer.handle();
  yield();
}
