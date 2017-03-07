#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "VirtualWire.h"
#include "ClunetMulticast.h"

extern "C" {
  #include "user_interface.h"

  //BME280 calibration consts
  static uint16_t dig_T1 = 28209;
  static int16_t  dig_T2 = 26575;
  static int16_t  dig_T3 = 50;
  static uint16_t dig_P1 = 37882;
  static int16_t  dig_P2 = -10739;
  static int16_t  dig_P3 = 3024;
  static int16_t  dig_P4 = 6140;
  static int16_t  dig_P5 = -8;
  static int16_t  dig_P6 = -7;
  static int16_t  dig_P7 = 9900;
  static int16_t  dig_P8 = -10230;
  static int16_t  dig_P9 = 4285;
  static uint8_t  dig_H1 = 75;
  static int16_t  dig_H2 = 354;
  static uint8_t  dig_H3 = 0;
  static int16_t  dig_H4 = 339;
  static int16_t  dig_H5 = 0;
  static uint8_t  dig_H6 = 30;

  #define BME280_S32_t int32_t
  #define BME280_U32_t uint32_t
  #define BME280_S64_t int64_t

  // Returns temperature in DegC, resolution is 0.01 DegC. Output value of “5123” equals 51.23 DegC.
  // t_fine carries fine temperature as global value
  BME280_S32_t t_fine;
  BME280_S32_t BME280_compensate_T_int32(BME280_S32_t adc_T)
  {
    BME280_S32_t var1, var2, T;
    var1 = ((((adc_T>>3) - ((BME280_S32_t)dig_T1<<1))) * ((BME280_S32_t)dig_T2)) >> 11;
    var2 = (((((adc_T>>4) - ((BME280_S32_t)dig_T1)) * ((adc_T>>4) - ((BME280_S32_t)dig_T1))) >> 12) * ((BME280_S32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    return T;
  }

  // Returns pressure in Pa as unsigned 32 bit integer in Q24.8 format (24 integer bits and 8 fractional bits).
  // Output value of “24674867” represents 24674867/256 = 96386.2 Pa = 963.862 hPa
  BME280_U32_t BME280_compensate_P_int64(BME280_S32_t adc_P)
  {
  BME280_S64_t var1, var2, p;
  var1 = ((BME280_S64_t)t_fine) - 128000;
  var2 = var1 * var1 * (BME280_S64_t)dig_P6;
  var2 = var2 + ((var1*(BME280_S64_t)dig_P5)<<17);
  var2 = var2 + (((BME280_S64_t)dig_P4)<<35);
  var1 = ((var1 * var1 * (BME280_S64_t)dig_P3)>>8) + ((var1 * (BME280_S64_t)dig_P2)<<12);
  var1 = (((((BME280_S64_t)1)<<47)+var1))*((BME280_S64_t)dig_P1)>>33;
  if (var1 == 0)
  {
  return 0; // avoid exception caused by division by zero
  }
  p = 1048576-adc_P;
  p = (((p<<31)-var2)*3125)/var1;
  var1 = (((BME280_S64_t)dig_P9) * (p>>13) * (p>>13)) >> 25;
  var2 = (((BME280_S64_t)dig_P8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (((BME280_S64_t)dig_P7)<<4);
  return (BME280_U32_t)p;
  }


  // Returns humidity in %RH as unsigned 32 bit integer in Q22.10 format (22 integer and 10 fractional bits).
  // Output value of “47445” represents 47445/1024 = 46.333 %RH
  BME280_U32_t bme280_compensate_H_int32(BME280_S32_t adc_H)
  {
  BME280_S32_t v_x1_u32r;
  v_x1_u32r = (t_fine - ((BME280_S32_t)76800));
  v_x1_u32r = (((((adc_H << 14) - (((BME280_S32_t)dig_H4) << 20) - (((BME280_S32_t)dig_H5) * v_x1_u32r)) +
  ((BME280_S32_t)16384)) >> 15) * (((((((v_x1_u32r * ((BME280_S32_t)dig_H6)) >> 10) * (((v_x1_u32r *((BME280_S32_t)dig_H3)) >> 11) + ((BME280_S32_t)32768))) >> 10) + ((BME280_S32_t)2097152)) *
  ((BME280_S32_t)dig_H2) + 8192) >> 14));
  v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((BME280_S32_t)dig_H1)) >> 4));
  v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
  v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
  return (BME280_U32_t)(v_x1_u32r>>12);
  }

}

const char *ssid = "espNet";
const char *pass = "esp8266A";

IPAddress ip(192,168,1,121);  //Node static IP
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

void setup() {
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

  ArduinoOTA.setHostname("meteo");

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
  
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  
  vw_set_rx_pin(14);
  vw_set_rx_inverted(1);
  
  vw_set_tx_pin(16);    //not neccesary
  
  vw_set_ptt_pin(0);    //not neccesary
  
  vw_setup(2000);  // Bits per sec
  vw_rx_start();   // Start the receiver PLL running

  clunetMulticastBegin();
}


int16_t  T = 0xFFFF;
uint32_t P= 0xFFFFFFFF;
uint16_t H = 0xFFFF;
uint16_t L = 0xFFFF;
uint16_t VCC;

void loop() {
    uint8_t buf[VW_MAX_MESSAGE_LEN];
    uint8_t buflen = VW_MAX_MESSAGE_LEN;

   if (vw_get_message(buf, &buflen)) { // Non-blocking
      digitalWrite(2, LOW); // Flash a light to show received good message

      //Serial.println("recieved rf");
      
      BME280_S32_t ut = ((uint32_t)buf[3]<<12) | ((uint32_t)buf[4]<<4) | ((buf[5]>>4) & 0x0F);
      BME280_S32_t up = ((uint32_t)buf[0]<<12) | ((uint32_t)buf[1]<<4) |((buf[2]>>4) & 0x0F);
      BME280_S32_t uh = ((uint32_t)buf[6]<<8) | (uint32_t)buf[7];

      L = ((uint16_t)buf[8]<<8) | (uint16_t)buf[9];         //*1
      if (L == 0xFFFF){ //reserved value for error
          L--;  //max valid value -> 65534
      }
      
      VCC = ((uint16_t)buf[10]<<8) | (uint16_t)buf[11];     //*100
      
      T = BME280_compensate_T_int32(ut);                             //*100
      P = (BME280_compensate_P_int64(up) / 256) * 1000 / 133.322;    //*1000
      H = (bme280_compensate_H_int32(uh) * 10) / 1024;               //*10
      
      digitalWrite(2, HIGH);
    }

    clunet_msg msg;
    if (clunetMulticastHandleMessages(&msg)){
      switch (msg.command){
        case CLUNET_COMMAND_TEMPERATURE:{
            if ((msg.size==1 && msg.data[0] == 0) 
                  || (msg.size == 2 && msg.data[0]==1 && msg.data[1]==2)){  //all devices or bmp/bme devices
                char buf[4];
                buf[0] = 1; buf[1] = 2; //bmp/bme
                memcpy(&buf[2], &T, 2);
                clunetMulticastSend(msg.src_address, CLUNET_COMMAND_TEMPERATURE_INFO, buf, sizeof(buf));
            }
          }
          break;
        case CLUNET_COMMAND_HUMIDITY:
          if (msg.size == 0){
              clunetMulticastSend(msg.src_address, CLUNET_COMMAND_HUMIDITY_INFO, (char*)&H, sizeof(H));
          }
          break;
        case CLUNET_COMMAND_PRESSURE:
          if (msg.size == 0){
            clunetMulticastSend(msg.src_address, CLUNET_COMMAND_PRESSURE_INFO, (char*)&P, sizeof(P));
          }
        break;
        case CLUNET_COMMAND_LIGHT_LEVEL:{
          if (msg.size == 0){
            char buf[3];
            buf[0] = 2; //значение люксометра
            memcpy(&buf[1], &L, 2);
            clunetMulticastSend(msg.src_address, CLUNET_COMMAND_LIGHT_LEVEL_INFO, buf, sizeof(buf));
          }
        }
        break;
        case CLUNET_COMMAND_VOLTAGE:{
          if (msg.size == 0){
            clunetMulticastSend(msg.src_address, CLUNET_COMMAND_VOLTAGE_INFO, (char*)&VCC, sizeof(VCC));
          }
        }
        break;
      }
    }

    ArduinoOTA.handle();
}

