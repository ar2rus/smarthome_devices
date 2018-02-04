#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
//#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "ESPAsyncTCP.h"
#include "ESPAsyncWebServer.h"

#define FASTLED_ALLOW_INTERRUPTS 0
//#define FASTLED_INTERRUPT_RETRY_COUNT 10
#include <FastLED.h>

#include <time.h>
#include <Ticker.h>

//#include "SerialDebug.h"
#include "Narodmon.h"
#include "Nixie.h"
//#include "NeoPixels.h"

//#include "InsideTermometer.h"

enum MODES {MODE_NONE,
            MODE_CLOCK,
            MODE_TERMOMETER, 
            MODE_HYGROMETR, 
            MODE_BAROMETER,
            MODE_TERMOMETER_INSIDE,
            MODE_ALARM};
enum MODES mode;
enum MODES led_mode;

#define ALARM_TIME 10000
uint32_t alarm_t;

enum EVENTS { EVENT_NONE, 
              EVENT_UPDATE_MODE,
              EVENT_NM_REQUEST, 
              EVENT_NM_RESPONSE, 
              EVENT_INSIDE_TERMOMETER_REQUEST,
              EVENT_MODE_CLOCK, 
              EVENT_MODE_TERMOMETER, 
              EVENT_MODE_BAROMETER,
              EVENT_MODE_INSIDE_TERMOMETER,
              EVENT_ALARM};
              
enum EVENTS event = EVENT_NONE;
enum EVENTS led_event = EVENT_NONE;

const char *ssid = "espNet";
const char *pass = "esp8266A";

IPAddress ip(192, 168, 1, 130); //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

time_t last_second = 0;


Narodmon* nm = new Narodmon(WiFi.macAddress());

#define LED_PIN 5
#define NUMPIXELS 6

// This is an array of leds.  One item for each led in your strip.
CRGB leds[NUMPIXELS];

//ESP8266WebServer server(80);
AsyncWebServer server(80);

void leds_clear(CRGB* _leds){
  for (int i=0; i<NUMPIXELS; i++){
    _leds[i] = CRGB::White;
  }
}

void leds_apply(CRGB* _leds){
  bool changed = false;
  for (int i=0; i<NUMPIXELS; i++){
    if (leds[i] != _leds[i]){
      changed = true;
      leds[i] = _leds[i];
    }
  }
  if (changed){
    FastLED.show();
  }
}

Ticker main_ticker;

void ticker_update() {
   time_t this_second;
   time(&this_second);
   
   if (this_second != last_second || event == EVENT_UPDATE_MODE){
      last_second = this_second;
      
      int h = (this_second / 3600) % 24;
      int m = (this_second / 60) % 60;
      int s = (this_second % 60);

      if (s == 0 && event != EVENT_UPDATE_MODE){
        #if DEBUG
          Serial.println("Event NM_REQUEST");
        #endif
        event = EVENT_NM_REQUEST;
      }else if (s >= 30 && s < 35){
        #if DEBUG
          Serial.println("Event MODE_TERMOMETER");
        #endif
        event =EVENT_MODE_TERMOMETER;
      }else if (s >=35 && s <40){
        #if DEBUG
          Serial.println("Event MODE_BAROMETER");
        #endif
        event = EVENT_MODE_BAROMETER;
      }else {
        #if DEBUG
        Serial.println("Event MODE_CLOCK");
        #endif
        event = EVENT_MODE_CLOCK;
      }

      switch (mode){
        case MODE_CLOCK:{
          char p = this_second%2;
          nixie_set(digit_code(h/10,1,0), digit_code(h%10,1,p), digit_code(m/10,1,0), digit_code(m%10,1,p), digit_code(s/10,1,0), digit_code(s%10,1,p));        
        }
         break; 
      }
      
   }
}

void config_time(float timezone_hours_offset, int daylightOffset_sec, 
    const char* server1, const char* server2, const char* server3){
  configTime((int)(timezone_hours_offset * 3600), daylightOffset_sec, server1, server2, server3);
  #if DEBUG
    Serial.println("Waiting for time");
  #endif
  
  time_t this_second = 0;
  while(!this_second) {
         time(&this_second);
         #if DEBUG
         Serial.print("-");
         #endif
         delay(100);
  }
  #if DEBUG
    Serial.println("");
  #endif
}

int oneWireRes = 5;

void config_narodmon(String apiKey, 
uint8_t use_latlng, double  lat, double lng, 
uint8_t radius){
  nm->setApiKey(apiKey);
  
  if(use_latlng){
     nm->setConfigLatLng(lat, lng);
  }else{
      nm->setConfigUseLatLng(0);
  }
   
  nm->setConfigRadius(radius);
}

void setup() {
  #if DEBUG
    Serial.begin(115200);
    Serial.println("\nBooting");
  #endif

  nixie_init();

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUMPIXELS).setCorrection(TypicalPixelString);
  FastLED.setBrightness(40);
  
  leds_clear(leds);
  FastLED.show();
  
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  #if DEBUG
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  #endif

  //server.on("/", handleRoot);

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/narodmon", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "NARODMON: " + nm->response);
  });

  server.on("/alarm", HTTP_GET, [](AsyncWebServerRequest *request){
    led_event = EVENT_ALARM;
    request->send(200);
  });
  
  server.onNotFound( [](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "File Not Found\n\n");
  });
  server.begin();

  ArduinoOTA.setHostname("esp-nixie");
  ArduinoOTA.begin();

  config_time(4, 0, "pool.ntp.org", "time.nist.gov", NULL);
  config_narodmon("9M5UhuQA2c8f8", 1, 53.2266, 50.1915, 8);
 
  //insideTermometerInit();

  mode = MODE_CLOCK;
  led_mode = MODE_NONE;
  main_ticker.attach_ms(1000, ticker_update);
  
  //serailDebug_init();
  
  #if DEBUG
    Serial.println("Setup done");
  #endif
  
}


CRGB _leds[NUMPIXELS];

void loop() {
  switch(event){
    case EVENT_NM_REQUEST:
      nm->request();
      break;
    case EVENT_NM_RESPONSE:
      #if DEBUG
        Serial.print("response: ");
        Serial.println(nm->response);
      _print("response: ");
      _println(nm->response);
      #endif
      break;
    /*case INSIDE_TERMOMETER_REQUEST:
      insideTermometerRequest();
      break;
    case MODE_INSIDE_TERMOMETER:
      mode = TERMOMETER_INSIDE;
      nixie_set(insideTermometerTemperature(), 2, 3);
      break;*/
    case EVENT_MODE_CLOCK:
      mode = MODE_CLOCK;     
      leds_clear(_leds);
      break; 
    case EVENT_MODE_TERMOMETER:
      leds_clear(_leds);
      if (nm->hasT()){
          //Serial.println(nm->getT()/10.0);
          int16_t t = nm->getT();
          nixie_set(t/10.0, 3, 1);
        
          uint8_t sign = t >= 0 ? 1 : 0;
          uint8_t e10 = (abs(t)/100) > 0;
          
          if (!sign){
            if (e10){
              _leds[2] = CRGB::Blue;
            }
            _leds[3] = CRGB::Blue;
            _leds[4] = CRGB::Blue;
          }
          mode = MODE_TERMOMETER;
      }else{
          mode = MODE_CLOCK;
      }
      break;
    case EVENT_MODE_BAROMETER:
        leds_clear(_leds);
        if (nm->hasP()){
          //Serial.println(nm->getP()/10.0);
          nixie_set(nm->getP()/10.0, 3, 1);
          mode = MODE_BAROMETER;
        }else{
          mode = MODE_CLOCK;
        }
        break;
  }

  event = EVENT_NONE;

  uint32_t m = millis();
  
  if (led_event == EVENT_ALARM){
    led_mode = MODE_ALARM;
    alarm_t = m;
  }

  switch (led_mode){
    case MODE_ALARM:
      if (millis()-alarm_t <= ALARM_TIME){
        _leds[0] = CRGB::Red;
        _leds[5] = CRGB::Red;
      }else{
        led_mode = MODE_NONE; 
        event = EVENT_UPDATE_MODE;
      }
    break;
  }

  if (event != EVENT_NONE){
    ticker_update();
  }
  
  led_event = EVENT_NONE;
  
  leds_apply(_leds);
  
  //nm->update();

  ArduinoOTA.handle();

  yield();
}

