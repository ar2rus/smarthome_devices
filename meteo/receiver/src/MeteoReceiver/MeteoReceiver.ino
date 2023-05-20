/**
 * Use 3.1.2 esp8266 core
 * lwip v2 Higher bandwidth; CPU 80 MHz
 * 1M (FS: 64K)
 *   
 * dependencies:
 * https://github.com/me-no-dev/ESPAsyncWebServer
 * https://github.com/ar2rus/ClunetMulticast
 *     
*/

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include "VirtualWire.h"

#include "MeteoReceiver.h"
#include "Credentials.h"


const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 121);     //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsAddr(192, 168, 1, 1);

AsyncWebServer server(8080);
ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);

//время получения последнего сообщения
uint32_t TIME;
//время получения последнего пакета данных (в том числе дубликаты) в мс
uint32_t M = INVALID_M;
//количество полученных дублирующих сообщений
uint8_t R;

int32_t T;
uint32_t P;
uint16_t H;
uint16_t L;
uint16_t VCC;

void reset() {
  T   = INVALID_T;
  P   = INVALID_P;
  H   = INVALID_H;
  L   = INVALID_L;
  VCC = INVALID_VCC;
}

void server_404(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "File Not Found\n\n");
}

String valueToString(bool valid, String value, String unit, boolean readable) {
  if (valid) {
    return readable ? "-" : "";
  } else {
    return value + ((readable && unit.length()) ? (" " + unit) : "");
  }
}

String tToString(boolean readable) {
  return valueToString(T == INVALID_T, String(T / 100.0), "*C", readable);
}

String pToString(boolean readable) {
  return valueToString(P == INVALID_P, String(P / 1000.0), "mm Hg", readable);
}

String hToString(boolean readable) {
  return valueToString(H == INVALID_H, String(H / 10.0), "%", readable);
}

String lToString(boolean readable) {
  return valueToString(L == INVALID_L, String(L), "Lx", readable);
}

String vccToString(boolean readable) {
  return valueToString(VCC == INVALID_VCC, String(VCC / 100.0), "V", readable);
}

String timeToString(boolean readable) {
  return valueToString(M == INVALID_M, String(TIME), "", readable);
}

String rToString(boolean readable) {
  return valueToString(R == 0, String(R), "messages", readable);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
 
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);

  reset();


  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet, dnsAddr);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(1000);
    ESP.restart();
  }

  Serial.println("Connected");

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

  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  vw_set_rx_pin(14);
  vw_set_rx_inverted(1);

  vw_set_tx_pin(16);    //not neccesary
  vw_set_ptt_pin(0);    //not neccesary

  vw_setup(2000);  // Bits per sec
  vw_rx_start();   // Start the receiver PLL running

  if (clunet.connect()){
    clunet.onPacketReceived([](clunet_packet* packet){
    switch (packet->command) {
      case CLUNET_COMMAND_TEMPERATURE: {
          if ((packet->size == 1 && packet->data[0] == 0)
              || (packet->size == 2 && packet->data[0] == 1 && packet->data[1] == 2)) { //all devices or bmp/bme devices

            int16_t T16 = (int16_t)T;
            char buf[3 + sizeof(T16)];
            buf[0] = 1; //num of devices
            buf[1] = T == INVALID_T ? 0xFF : 2; //error / bmp/bme
            buf[2] = 0; //device id

            memcpy(&buf[3], &T16, sizeof(T16));
            clunet.send(packet->src, CLUNET_COMMAND_TEMPERATURE_INFO, buf, sizeof(buf));
          }
        }
        break;
      case CLUNET_COMMAND_HUMIDITY:
        if (packet->size == 0) {
          clunet.send(packet->src, CLUNET_COMMAND_HUMIDITY_INFO, (char*)&H, sizeof(H));
        }
        break;
      case CLUNET_COMMAND_PRESSURE:
        if (packet->size == 0) {
          clunet.send(packet->src, CLUNET_COMMAND_PRESSURE_INFO, (char*)&P, sizeof(P));
        }
        break;
      case CLUNET_COMMAND_LIGHT_LEVEL: {
          if (packet->size == 0) {
            char buf[3];
            buf[0] = 2; //значение люксометра
            memcpy(&buf[1], &L, 2);
            clunet.send(packet->src, CLUNET_COMMAND_LIGHT_LEVEL_INFO, buf, sizeof(buf));
          }
        }
        break;
      case CLUNET_COMMAND_VOLTAGE: {
          if (packet->size == 0) {
            clunet.send(packet->src, CLUNET_COMMAND_VOLTAGE_INFO, (char*)&VCC, sizeof(VCC));
          }
        }
        break;
      case CLUNET_COMMAND_METEO: {
          if (packet->size == 0) {
            sendMeteoInfo(packet->src);
          }
        }
        break;
      /*case CLUNET_COMMAND_DEBUG:{
        uint32_t m = millis() - M;
        clunet.send(packet->src, CLUNET_COMMAND_DEBUG, (char*)&m, sizeof(m));
        }
        break;*/
      }
    });
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    int resp_format = -1;
    switch (request->args()) {
      case 0:
        resp_format = 0;  //txt
        break;
      case 1:
        if (request->hasArg("fmt")) {
          String fmt = request->arg("fmt");
          if (fmt == "txt") {
            resp_format = 0;  //txt
          } else if (fmt == "json") {
            resp_format = 1;  //json
          } else if (fmt == "csv") {
            resp_format = 2;  //csv
          }
        }
        break;
    }

    switch (resp_format) {
      case 0: {
          String message;
          if (M == INVALID_M) {
            message = "No data received";
          } else {

            message = "Time: " + timeToString(true);
            message += " (" + String((millis() - M) / 1000) + " seconds ago, " + rToString(true) + ")";

            message += "\n";
            message += "\nT: " +  tToString(true);
            message += "\nH: " +  hToString(true);
            message += "\nP: " +  pToString(true);
            message += "\nL: " +  lToString(true);
            message += "\nV: " +  vccToString(true);
          }
          request->send(200, "text/plain", message);
        }
        break;
      case 1: {
          String message = "{";
          if (M != INVALID_M) {
            message += "\"time\":\"" + timeToString(false) + "\",";
            message += "\"t\":\"" + tToString(false) + "\",";
            message += "\"h\":\"" + hToString(false) + "\",";
            message += "\"p\":\"" + pToString(false) + "\",";
            message += "\"l\":\"" + lToString(false) + "\",";
            message += "\"v\":\"" + vccToString(false) + "\"";
          }
          message += "}";
          request->send(200, "application/json", message);
        }
        break;
      case 2: {
          String message = timeToString(false);
          message += ";";
          message += rToString(false);
          message += ";";
          message += tToString(false);
          message += ";";
          message += hToString(false);
          message += ";";
          message += pToString(false);
          message += ";";
          message += lToString(false);
          message += ";";
          message += vccToString(false);

          request->send(200, "text/plain", message);
        }
        break;
      default:
        server_404(request);
    }
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/rssi", HTTP_GET, [](AsyncWebServerRequest * request) {
    long rssi = WiFi.RSSI();
    int quality = 2 * (rssi + 100);
    request->send(200, "text/plain", String(rssi) + " (" + quality + ")");
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) {
    ESP.restart();
  });

  server.onNotFound( [](AsyncWebServerRequest * request) {
    server_404(request);
  });

  server.begin();
}

void sendMeteoInfo(uint8_t address) {
  char buf[9];
  buf[0] = ((L == INVALID_L ? 0 : 1) << 3)
           | ((P == INVALID_P ? 0 : 1) << 2)
           | ((H == INVALID_H ? 0 : 1) << 1)
           | ((T == INVALID_T ? 0 : 1) << 0);

  int16_t T16 = (int16_t)T;
  memcpy(&buf[1], &T16, sizeof(T16));
  memcpy(&buf[3], &H, sizeof(H));
  int16_t P16 = P / 100;
  memcpy(&buf[5], &P16, sizeof(P16));
  memcpy(&buf[7], &L, sizeof(L));
  clunet.send(address, CLUNET_COMMAND_METEO_INFO, buf, sizeof(buf));
}

void loop() {
  uint8_t buf[VW_MAX_MESSAGE_LEN];
  uint8_t buflen = VW_MAX_MESSAGE_LEN;

  uint32_t m = millis();
  uint32_t dm = m - M;

  if (dm > MESSAGE_USE_TIME) { //15 min
    reset();
  }

  if (dm >= MIN_DELAY_BETWEEN_PACKETS){
    digitalWrite(2, HIGH);  //led off
    if (vw_get_message(buf, &buflen)) { // Non-blocking
      digitalWrite(2, LOW); // Flash a light to show received good message
  
      BME280_S32_t ut = ((uint32_t)buf[3] << 12) | ((uint32_t)buf[4] << 4) | ((buf[5] >> 4) & 0x0F);
      BME280_S32_t up = ((uint32_t)buf[0] << 12) | ((uint32_t)buf[1] << 4) | ((buf[2] >> 4) & 0x0F);
      BME280_S32_t uh = ((uint32_t)buf[6] << 8) | (uint32_t)buf[7];
  
      L = ((uint16_t)buf[8] << 8) | (uint16_t)buf[9];       //*1
      if (L == INVALID_L) { //reserved value for error
        L--;  //max valid value -> 65534
      }
  
      VCC = ((uint16_t)buf[10] << 8) | (uint16_t)buf[11];   //*100
  
      T = BME280_compensate_T_int32(ut);                             //*100
      P = (BME280_compensate_P_int64(up) / 256) * 1000 / 133.322;    //*1000
      H = (bme280_compensate_H_int32(uh) * 10) / 1024;               //*10
  
      if (dm > MAX_DELAY_BETWEEN_PACKETS) { //прошло больше секунды -> это не дублирующее сообщение
        TIME = m;
        R = 1;
        sendMeteoInfo(CLUNET_ADDRESS_BROADCAST);
      } else {
        R++;
      }
  
      M = m;
    }
  }
  
  ArduinoOTA.handle();
  yield();
}
