#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <time.h>
#include <Ticker.h>

#include "SerialDebug.h"
#include "Narodmon.h"
#include "Nixie.h"
#include "NeoPixels.h"

//#include "InsideTermometer.h"

enum MODES {CLOCK_SETUP, CLOCK, TERMOMETER_INSIDE, TERMOMETER, HUMIDITY, BAROMETER, ALARM};
enum MODES mode;

enum EVENTS { NONE, 
              NM_REQUEST, 
              NM_RESPONSE, 
              INSIDE_TERMOMETER_REQUEST,
              MODE_CLOCK, 
              MODE_TERMOMETER, 
              MODE_BAROMETER,
              MODE_INSIDE_TERMOMETER};
enum EVENTS event = NONE;

const char *ssid = "espNet";
const char *pass = "esp8266A";

IPAddress ip(192, 168, 1, 130); //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

time_t last_second = 0;


Narodmon* nm = new Narodmon(WiFi.macAddress());

Adafruit_NeoPixel* neopixels_strip = new Adafruit_NeoPixel(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
uint32_t minus_color = neopixels_strip->Color(0, 0, 20);


Ticker main_ticker;

void ticker_update() {
   time_t this_second;
   time(&this_second);
   
   if (this_second != last_second){
      last_second = this_second;
      
      int h = (this_second / 3600) % 24;
      int m = (this_second / 60) % 60;
      int s = (this_second % 60);

      if (s == 0){
        #if DEBUG
          Serial.println("Event NM_REQUEST");
        #endif
        event = NM_REQUEST;
      }

      /*if (s == 3){
        #if DEBUG
        Serial.println("Event INSIDE_TERMOMETER_REQUEST");
        #endif
        event = INSIDE_TERMOMETER_REQUEST;
      }

      if (s == 5){
        #if DEBUG
        Serial.println("Event MODE_INSIDE_TERMOMETER");
        #endif
        event = MODE_INSIDE_TERMOMETER;
      }*/

      if (s == 10){
        #if DEBUG
          Serial.println("Event NM_RESPONSE");
        #endif
        event = NM_RESPONSE;
      }

      if (s == 15 || s == 35){
        #if DEBUG
          Serial.println("Event MODE_TERMOMETER");
        #endif
        event = MODE_TERMOMETER;
      }

      if (s == 20 || s == 40){
        #if DEBUG
          Serial.println("Event MODE_BAROMETER");
        #endif
        event = MODE_BAROMETER;
      }

      if (/*s == 10 || */s == 30 || s == 45){
        #if DEBUG
        Serial.println("Event MODE_CLOCK");
        #endif
        event = MODE_CLOCK;
      }

      switch (mode){
        case CLOCK:{
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

  neopixels_strip->begin();
  neopixels_clear(neopixels_strip);
  neopixels_strip->show();
  
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

  ArduinoOTA.setHostname("esp-nixie");
  ArduinoOTA.begin();

  config_time(4, 0, "pool.ntp.org", "time.nist.gov", NULL);
  config_narodmon("9M5UhuQA2c8f8", 1, 53.2266, 50.1915, 8);
 
  //insideTermometerInit();

  mode = CLOCK;
  main_ticker.attach(50, ticker_update);
  
  //serailDebug_init();
  
  #if DEBUG
    Serial.println("Setup done");
  #endif  
}


void loop() {

  switch(event){
    case NM_REQUEST:
      nm->request();
      break;
    case NM_RESPONSE:
      #if DEBUG
        Serial.print("response: ");
        Serial.println(nm->response);
      #endif
      _print("response: ");
      _println(nm->response);
      break;
    /*case INSIDE_TERMOMETER_REQUEST:
      insideTermometerRequest();
      break;
    case MODE_INSIDE_TERMOMETER:
      mode = TERMOMETER_INSIDE;
      nixie_set(insideTermometerTemperature(), 2, 3);
      break;*/
    case MODE_CLOCK:
      mode = CLOCK;
      neopixels_clear(neopixels_strip);
      neopixels_strip->show();
      break; 
    case MODE_TERMOMETER:
      neopixels_clear(neopixels_strip);
      if (nm->hasT()){
          //Serial.println(nm->getT()/10.0);
          int16_t t = nm->getT();
          nixie_set(t/10.0, 3, 1);
        
          uint8_t sign = t >= 0 ? 1 : 0;
          uint8_t e10 = (abs(t)/100) > 0;
          
          if (!sign){
            if (e10){
              neopixels_strip->setPixelColor(2, minus_color);
            }
            neopixels_strip->setPixelColor(3, minus_color);
            neopixels_strip->setPixelColor(4, minus_color);
          }
          neopixels_strip->show();  
          mode = TERMOMETER;
      }else{
          mode = CLOCK;
      }
      break;
    case MODE_BAROMETER:
        neopixels_clear(neopixels_strip);
        neopixels_strip->show();
        if (nm->hasP()){
          //Serial.println(nm->getP()/10.0);
          nixie_set(nm->getP()/10.0, 3, 1);
          mode = BAROMETER;
        }else{
          mode = CLOCK;
        }
        break;
  }

  event = NONE;

  nm->update();
  //serailDebug_update();
    
  ArduinoOTA.handle();
}
