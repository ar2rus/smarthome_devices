#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <Adafruit_NeoPixel.h>
#include <time.h>

#include <SPI.h>

#include <OneWire.h>r

#include "SerialDebug.h"
#include "Narodmon.h"

long t;

#define LED_PIN 5
#define NUMPIXELS 6

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

OneWire  ds(16);  // GPIO16 (a 4.7K resistor is necessary)

const char *ssid = "espNet";
const char *pass = "esp8266A";

IPAddress ip(192, 168, 1, 130); //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

time_t this_second = 0;
time_t last_second = 0;

#define SPI_PIN_SS 15

#define _BV(bit) (1 << (bit))
#define _TB(v, bit) (v & _BV(bit))


#define digit_code(digit, enable, point) (((!enable & 0x01)<<0x07) | ((point & 0x01)<<0x06) | (digit & 0x0F))
#define digit_off (digit_code(0,0,0))

#define digit_value(code) (code & 0x0F)
#define digit_enable(code) (!_TB(code, 7))
#define digit_point(code) (_TB(code, 6))

#define digit_code_group_h(d) ((_TB(d, 0) ? _BV(4) : 0) | (_TB(d, 1) ? _BV(2) : 0) | (_TB(d, 2) ? _BV(1) : 0) | (_TB(d, 3) ? _BV(3) : 0))
#define digit_code_group_l(d) ((_TB(d, 0) ? _BV(7) : 0) | (_TB(d, 1) ? _BV(5) : 0) | (_TB(d, 2) ? _BV(4) : 0) | (_TB(d, 3) ? _BV(6) : 0))

#define digit_group_h(d, r0, r1, r2)(digit_code_group_h(digit_value(d)) | (digit_point(d) ? _BV(7) : 0) |\
  (digit_enable(d) && r0 ? _BV(0) : 0) | (digit_enable(d) && r1 ? _BV(5) : 0) | (digit_enable(d) && r2 ? _BV(6) : 0))
#define digit_group_l(d, r0, r1, r2)(digit_code_group_l(digit_value(d)) | (digit_point(d) ? _BV(3) : 0) |\
  (digit_enable(d) && r0 ? _BV(2) : 0) | (digit_enable(d) && r1 ? _BV(1) : 0) | (digit_enable(d) && r2 ? _BV(0) : 0))

#define NIXIE_UPDATE_PERIOD  2

long nixie_t;
char nixie_cnt;
char digits[6];

Narodmon* nm = new Narodmon(/*"esp8266.nixie.clock"*/WiFi.macAddress(), "9M5UhuQA2c8f8");

extern "C" {
  #include "user_interface.h"
}

os_timer_t esp_timer;
os_timer_t clock_timer;
os_timer_t event_timer;

void nixie_set(char d0, char d1, char d2, char d3, char d4, char d5){
    digits[0]=d0;
    digits[1]=d1;
    digits[2]=d2;
    digits[3]=d3;
    digits[4]=d4;
    digits[5]=d5;
    
    //nixie_t = 0;
    //nixie_update();
}

void nixie_clear(){
  char d = digit_code(0,0,0);
  nixie_set(d,d,d,d,d,d);
}

void nixieTimerCallback(void *pArg) {
    if (++nixie_cnt > 2){
        nixie_cnt = 0;
    }

    SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
    digitalWrite(SPI_PIN_SS, LOW);
    char d_h = digits[3+nixie_cnt];
    SPI.transfer(digit_group_h(d_h, nixie_cnt==0, nixie_cnt==1, nixie_cnt==2));
    char d_l = digits[0+nixie_cnt];
    SPI.transfer(digit_group_l(d_l, nixie_cnt==0, nixie_cnt==1, nixie_cnt==2));
    digitalWrite(SPI_PIN_SS, HIGH); 
    SPI.endTransaction();
}

void clockTimerCallback(void *pArg) {
   time_t this_second;
   time(&this_second);
   if (this_second != last_second){
      last_second = this_second;
      
      int h = (this_second / 3600) % 24;
      int m = (this_second / 60) % 60;
      int s = (this_second % 60);

      char p = this_second%2;
      nixie_set(digit_code(h/10,1,p), digit_code(h%10,1,p), digit_code(m/10,1,p), digit_code(m%10,1,p), digit_code(s/10,1,p), digit_code(s%10,1,p));
  }
}

bool event_request_narodmon = false;
bool event_response_narodmon = false;

void eventTimerCallback(void *pArg) {
   time_t this_second;
   time(&this_second);
      
   int h = (this_second / 3600) % 24;
   int m = (this_second / 60) % 60;
   int s = (this_second % 60);

   if (s == 0){
    event_request_narodmon = true;
   }

   if (s == 10){
    event_response_narodmon = true; 
   }
}

void setup() {

  SPI.begin();
  nixie_clear();
  
  pinMode(SPI_PIN_SS, OUTPUT);
  
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

  ArduinoOTA.setHostname("esp-nixie");

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

   strip.begin();
   //strip.setPixelColor(1, strip.Color(255, 0, 0));
   strip.show();

  configTime(4 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("\nWaiting for time");
  while(!this_second)
     {
         time(&this_second);
         Serial.print("-");
         delay(100);
     }
  //strip.setPixelColor(5, strip.Color(255, 0, 0));
  strip.show();

  t = millis();

  nm->setConfigRadius(8);
  
  serailDebug_init();

  os_timer_setfn(&esp_timer, nixieTimerCallback, NULL);
  os_timer_arm(&esp_timer, NIXIE_UPDATE_PERIOD, true);

  os_timer_setfn(&clock_timer, clockTimerCallback, NULL);
  os_timer_arm(&clock_timer, 50, true);

  os_timer_setfn(&event_timer, eventTimerCallback, NULL);
  os_timer_arm(&event_timer, 1000, true);
}

int code = -1;
byte addr[8]; 
float temperature;

long start_conv_t = -1;

int startTConversion(){
  if (!ds.search(addr)) {
    ds.reset_search();
    //not found
    return 1;
  }
  ds.reset_search(); 
 
  if (OneWire::crc8(addr, 7) != addr[7]) {
      //Serial.println("CRC is not valid!");
      return 2;
  }

  ds.reset();            
  ds.select(addr);        
  ds.write(0x44);

  return 0;
}

float getTemp(){
  byte data[12];  
  
  ds.reset();
  ds.select(addr);    
  ds.write(0xBE);          

  for (int i = 0; i < 9; i++) {           
    data[i] = ds.read();  
  }

  int raw = (data[1] << 8) | data[0]; 
  if (data[7] == 0x10) raw = (raw & 0xFFF0) + 12 - data[6];  
  return raw / 16.0;
} 


// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

//Theatre-style crawling lights with rainbow effect
void theaterChaseRainbow(uint8_t wait) {
  for (int j=0; j < 256; j++) {     // cycle all 256 colors in the wheel
    for (int q=0; q < 3; q++) {
      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, Wheel( (i+j) % 255));    //turn every third pixel on
      }
      strip.show();
       ArduinoOTA.handle();
      delay(wait);

      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, 0);        //turn every third pixel off
      }
    }
  }
}


// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait) {
  uint16_t i, j;

  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
    for(i=0; i< strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    ArduinoOTA.handle();
    delay(wait);
  }
}


void loop() {

if (event_request_narodmon){
    event_request_narodmon = false;
    nm->request();
}

if (event_response_narodmon){
    event_response_narodmon = false;
   _print("response: ");
   _println(nm->response);
}

       /*  strip.setPixelColor(0, strip.Color(127*((this_second / 3600) % 24)/24, 0, 0));
         strip.setPixelColor(1, strip.Color(127*((this_second / 3600) % 24)/24, 0, 0));
         
         strip.setPixelColor(2, strip.Color(0, 127*((this_second / 60) % 60)/60, 0));
         strip.setPixelColor(3, strip.Color(0, 127*((this_second / 60) % 60)/60, 0));
         
         strip.setPixelColor(4, strip.Color(0, 0, 127*(this_second%60)/60));
         strip.setPixelColor(5, strip.Color(0, 0, 127*(this_second%60)/60));
         strip.show();*/
     
  /*  }else if (delta < 20000){
            
      switch (code){
        case -1:
          code = startTConversion();
          start_conv_t = millis();
          break;
        case 0:
        if (millis() - start_conv_t > 1000){
          temperature = getTemp();
          code = 3;
        }
          break;
       }

        
      switch(code){
        case 0:
        case 1:
        case 2:
       nixie_set(digit_code(code,1,0), digit_off, digit_off, digit_off, digit_off, digit_off);
          break;
        case 3:
          int t_int = abs(temperature) * 100;
          int t_int_10 = t_int / 1000;
          int t_int_1 = t_int / 100;
          int t_int_01 = t_int / 10;
          int t_int_001 = t_int % 10;
          nixie_set(digit_off, digit_off, digit_code(t_int_10,t_int_10>0,0), digit_code(t_int_1,1,1), digit_code(t_int_01,1,0), digit_code(t_int_001,t_int_001 > 0,0));
          code = 4;
      }
    }else{
        t = millis();
        code = -1;
     }*/

/*
    long tmp = millis();
    if (tmp-t > 500){
      if (++n>9){
        
n=0;
}
      nixie_set(n, n, n, n, n, n);
      
        t = tmp;
      }
  */

    nm->update();
    serailDebug_update();
    
    ArduinoOTA.handle();
    
}
