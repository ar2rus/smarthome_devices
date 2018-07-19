#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "ESPAsyncTCP.h"
#include "ESPAsyncWebServer.h"

#define FASTLED_ALLOW_INTERRUPTS 0
//#define FASTLED_INTERRUPT_RETRY_COUNT 10
#include <FastLED.h>

#include <time.h>

#include "Narodmon.h"
#include "Nixie.h"

//#include "NeoPixels.h"
#include "InsideTermometer.h"

#include "Credentials.h"
#include "Tasks.h"

#define ALARM_DURATION 15000

IPAddress ip(192, 168, 1, 130); //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

TaskWrapper* tw = new TaskWrapper();
Narodmon* nm = new Narodmon(WiFi.macAddress());

AsyncWebServer server(80);

#define LED_PIN 5
#define NUMPIXELS 6

bool leds_on = true;
CRGB leds[NUMPIXELS], _leds[NUMPIXELS];

void leds_clear(CRGB* _leds) {
  fill_solid(_leds, NUMPIXELS, leds_on ? CRGB::White : CRGB::Black);
}

void leds_apply(CRGB* _leds) {
  bool changed = false;
  for (int i = 0; i < NUMPIXELS; i++) {
    if (leds[i] != _leds[i]) {
      changed = true;
      leds[i] = _leds[i];
    }
  }
  if (changed) {
    FastLED.setBrightness(40);
    FastLED.show();
  }
}


void config_time(float timezone_hours_offset, int daylightOffset_sec,
                 const char* server1, const char* server2, const char* server3) {
  configTime((int)(timezone_hours_offset * 3600), daylightOffset_sec, server1, server2, server3);
  INFO("Timezone: %f; daylightOffset: %d", timezone_hours_offset, daylightOffset_sec);
}

void config_narodmon(String apiKey, uint8_t use_latlng, double lat, double lng, uint8_t radius) {
  nm->setApiKey(apiKey);
  if (use_latlng) {
    nm->setConfigLatLng(lat, lng);
  } else {
    nm->setConfigUseLatLng(0);
  }
  nm->setConfigRadius(radius);
}

void exec_none() {
  nixie_clear();
  leds_clear(_leds);
  leds_apply(_leds);
}

uint8_t available_clock() {
  time_t this_second = 0;
  time(&this_second);
  return this_second;
}

void _clock(){
  time_t this_second;
  time(&this_second);
  if (this_second){
    int h = (this_second / 3600) % 24;
    int m = (this_second / 60) % 60;
    int s = (this_second % 60);
    char p = this_second % 2;
    nixie_set(digit_code(h / 10, 1, 0), digit_code(h % 10, 1, p), digit_code(m / 10, 1, 0), digit_code(m % 10, 1, p), digit_code(s / 10, 1, 0), digit_code(s % 10, 1, p));
  }else{
    nixie_clear();
  }
}

void exec_clock() {
  _clock();
  leds_clear(_leds);
  leds_apply(_leds);
}

uint8_t available_t() {
  return nm->hasT();
}

void exec_t() {
  int16_t t = nm->getT();
  nixie_set(t / 10.0, 3, 1);

  uint8_t sign = t >= 0 ? 1 : 0;
  uint8_t e10 = (abs(t) / 100) > 0;

  leds_clear(_leds);
  if (!sign) {
    if (e10) {
      _leds[2] = CRGB::Blue;
    }
    _leds[3] = CRGB::Blue;
    _leds[4] = CRGB::Blue;
  }
  leds_apply(_leds);
}

uint8_t available_p() {
  return nm->hasP();
}

void exec_p() {
  nixie_set(nm->getP() / 10.0, 3, 1);
  leds_clear(_leds);
  leds_apply(_leds);
}

uint8_t available_h() {
  return nm->hasH();
}

void exec_h() {
  int16_t t = nm->getH();
  nixie_set(t / 10, 3);
  leds_clear(_leds);
  leds_apply(_leds);
}

uint8_t available_t_inside() {
  return insideTermometerHasT();
}

void exec_t_inside() {
  nixie_set(insideTermometerTemperature(), 2, 3);
  leds_clear(_leds);
  leds_apply(_leds);
}


void config_modes(uint32_t clock_duration, 
  uint32_t t_duration, uint32_t p_duration, uint32_t h_duration,
  uint32_t t_inside_duration) {
  tw->reset();
  tw->addContinuousTask(clock_duration, available_clock, exec_clock);
  tw->addContinuousTask(t_duration, available_t, exec_t);
  if (p_duration){
    tw->addContinuousTask(clock_duration, available_clock, exec_clock);
    tw->addContinuousTask(p_duration, available_p, exec_p);
  }
  if (h_duration){
    tw->addContinuousTask(clock_duration, available_clock, exec_clock);
    tw->addContinuousTask(h_duration, available_h, exec_h);
  }
  if (t_inside_duration){
    tw->addContinuousTask(clock_duration, available_clock, exec_clock);
    tw->addContinuousTask(t_inside_duration, available_t_inside, exec_t_inside);
  }
  
  //  tw->addPeriodicalTask(1000, 60000, []() {
  //    insideTermometerRequest();
  //  });

  tw->addPeriodicalTask(10000, 120000, []() {
    nm->request();
  });
}


uint8_t ota_progress;

/*void rainbow_beat() {
  uint8_t beatA = beatsin8(17, 0, 255);                        // Starting hue
  uint8_t beatB = beatsin8(13, 0, 255);
  fill_rainbow(leds, NUMPIXELS, (beatA+beatB)/2, 8);            // Use FastLED's fill_rainbow routine.
}*/

#define BPM       60
#define DIMMEST   0
#define BRIGHTEST 255

void setup() {
  DEBUG("Booting");
  nixie_init();

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUMPIXELS).setCorrection(TypicalPixelString);
  leds_clear(_leds);
  leds_apply(_leds);

  WiFi.mode(WIFI_STA);

  WiFi.begin(AP_SSID, AP_PASSWORD);
  WiFi.config(ip, gateway, subnet);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(1000);
    ESP.restart();
  }
  
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
    tw->callContinuousTask(10000, available_clock, exec_clock);
    request->send(200);
  });

  server.on("/t", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(10000, available_t, exec_t);
    request->send(200);
  });

  server.on("/p", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(10000, available_p, exec_p);
    request->send(200);
  });

  server.on("/h", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(10000, available_h, exec_h);
    request->send(200);
  });

  server.on("/alarm", HTTP_GET, [](AsyncWebServerRequest * request) {
    tw->callContinuousTask(ALARM_DURATION, NULL, []() {
      _clock();
    /*  for (int i = 0; i < NUMPIXELS; i++) {
        _leds[i] = CRGB::Red;
      }
      leds_apply(_leds);*/
      
  static uint16_t hue16 = 0;
  hue16 += 9;
  fill_rainbow( leds, NUMPIXELS, hue16 / 256, 0);

  // set the brightness to a sine wave that moves with a beat
  uint8_t bright = beatsin8( BPM, DIMMEST, BRIGHTEST);
  FastLED.setBrightness( bright );
  FastLED.show();


    });
    
    request->send(200);
  });

  server.on("/led", HTTP_GET, [](AsyncWebServerRequest * request) {
    leds_on = !leds_on;
    request->send(200);
  });

  server.onNotFound( [](AsyncWebServerRequest * request) {
    request->send(404, "text/plain", "File Not Found\n\n");
  });
  server.begin();

  ArduinoOTA.setHostname("esp-nixie");
  ArduinoOTA.onStart([]() {
    //if (ArduinoOTA.getCommand() == U_FLASH)
    //  type = "sketch";
    //else // U_SPIFFS
    //  type = "filesystem";
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()

    ota_progress = 0;
    tw->callContinuousTask([]() {
      leds_clear(_leds);
      for (int i = 0; i < NUMPIXELS; i++) {
        if (ota_progress >= (i + 1) * 100 / NUMPIXELS) {
          _leds[i] = CRGB::Orange;
        } else {
          break;
        }
      }
      leds_apply(_leds);
      nixie_set(ota_progress, 5);
    });
  });

  ArduinoOTA.onEnd([]() {
    tw->callContinuousTask(exec_none);
    tw->update();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    ota_progress = (progress / (total / 100));
    tw->update(); //main loop does not call while OTA
  });


  ArduinoOTA.begin();

  config_time(4, 0, "pool.ntp.org", "time.nist.gov", NULL);
  config_narodmon("9M5UhuQA2c8f8", 1, 53.2266, 50.1915, 8);
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

  ArduinoOTA.handle();
  yield();
}

