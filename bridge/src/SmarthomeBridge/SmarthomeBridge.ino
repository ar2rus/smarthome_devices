/**
 * Flash size: 4M (FS: 1Mb / OTA: 1019 Kb) !!!
 * 
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include <time.h>

#include "SmarthomeBridge.h"
#include "Credentials.h"

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 120);     //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer server(80);
ClunetMulticast clunet(CLUNET_ID, CLUNET_DEVICE);


#define UART_MESSAGE_CODE_CLUNET 1
#define UART_MESSAGE_CODE_FIRMWARE 2
#define UART_MESSAGE_CODE_DEBUG 10

const char UART_MESSAGE_PREAMBULE[] = {0xC9, 0xE7};

uint8_t uart_send_message(char code, char* data, uint8_t length){
  if (Serial.availableForWrite() < length + 5){
    return 0;
  }
  
  uint8_t buf_length = length + 2;
  
  char buf[buf_length];
  buf[0] = length + 3;
  buf[1] = code;
  
  if (length){
    memcpy((void*)(buf + 2), data, length);
  }

  Serial.write((char*)UART_MESSAGE_PREAMBULE, 2);
  Serial.write(buf, buf_length);
  Serial.write(check_crc(buf, buf_length));

  return 1;
}

int x = 0;


void config_time(float timezone_hours_offset, int daylightOffset_sec, const char* server1, const char* server2, const char* server3) {
  configTime((int)(timezone_hours_offset * 3600), daylightOffset_sec, server1, server2, server3);
  //INFO("Timezone: %f; daylightOffset: %d", timezone_hours_offset, daylightOffset_sec);
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("\n\nHello");
  
  Serial.begin(38400, SERIAL_8N1);
  Serial.println("Booting");

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet);

  pinMode(LED_BLUE_PORT, OUTPUT);  
  analogWrite(LED_BLUE_PORT, 12);
  
  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial1.println("Connection Failed! Rebooting...");
    delay(1000);
    ESP.restart();
  }
  Serial1.println("Connected");

  Serial.swap();
  Serial.flush();
  Serial.setRxBufferSize(256);  //as default

  ArduinoOTA.setHostname("smarthome-bridge");
  ArduinoOTA.begin();

  config_time(4, 0, "pool.ntp.org", "time.nist.gov", NULL);

  if (clunet.connect()){
    clunet.onPacketSniff([](clunet_packet* packet){
      //Serial1.println("received multicast: code: " + String(packet->command) + "; src: " + String(packet->src)+"; dst: " + String(packet->dst) + "; length: " + String(packet->size));
      if (CLUNET_MULTICAST_DEVICE(packet->src)){
        //Serial1.println("clunet: code: " + String(packet->command) + "; src: " + String(packet->src)+"; dst: " + String(packet->dst) + "; length: " + String(packet->size));
        //packet->src= 0; //TODO: kill me
        uart_send_message(UART_MESSAGE_CODE_CLUNET, (char*)packet, 4 + packet->size);
        //Serial1.println("send uart: code: " + String(packet->command) + "; src: " + String(packet->src)+"; dst: " + String(packet->dst) + "; length: " + String(packet->size));
      }else{
        //Serial1.println("skip");
      }
    });
  }

  //TODO: debug
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(200, "text/plain", "value: " + String(x));
  });

  server.begin();
}

char check_crc(char* data, uint8_t size){
  uint8_t crc=0;
  for (uint8_t i=0; i<size; i++){
    uint8_t inbyte = data[i];
    for (uint8_t j=0; j<8; j++){
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix){
        crc ^= 0x8C;
      }
      inbyte >>= 1;
    }
  }
  return crc;
}

void on_uart_message(uint8_t code, char* data, uint8_t length){
  switch(code){
    case UART_MESSAGE_CODE_CLUNET:
      if (data && length >= 4){
        clunet_packet* packet = (clunet_packet*)data;
        
        Serial1.println("received uart: code: " + String(packet->command) + "; src: " + String(packet->src)+"; dst: " + String(packet->dst) + "; length: " + String(packet->size));
        if (!CLUNET_MULTICAST_DEVICE(packet->src)){
          //TODO: move to buffer at first
          clunet.send_fake(packet->src, packet->dst, packet->command, packet->data, packet->size);
          //Serial1.println("send multicast: code: " + String(packet->command) + "; src: " + String(packet->src)+"; dst: " + String(packet->dst) + "; length: " + String(packet->size));
        }else{
          //Serial1.println("skip"); 
        }
      }
      break;
    case UART_MESSAGE_CODE_FIRMWARE:
      if (data && length >= 4){
        clunet_packet* packet = (clunet_packet*)data;
        x = packet->command;
        clunet.send_fake(packet->src, packet->dst, packet->command, packet->data, packet->size);
      }
      break;
    case UART_MESSAGE_CODE_DEBUG:
      Serial1.println("debug message received: " + String((int8_t)data[0]));
      break;
  }
}

#define UART_RX_BUF_LENGTH 256
volatile char uart_rx_data[UART_RX_BUF_LENGTH];
volatile unsigned char uart_rx_data_len = 0;

void analyze_uart_rx_trim(uint8_t offset){
  if (offset <= uart_rx_data_len){
    uart_rx_data_len -= offset;
    if (uart_rx_data_len){
      memmove((void*)uart_rx_data, (void*)(uart_rx_data + offset), uart_rx_data_len);
    }
  }
}

void analyze_uart_rx(void(*f)(uint8_t code, char* data, uint8_t length)){
  while (uart_rx_data_len > 1){
    //Serial1.println("len: " + uart_rx_data_len);
    uint8_t uart_rx_preambula_offset = uart_rx_data_len - 1;  //первый байт преамбулы может быть прочитан, а второй еще не пришел
    for (uint8_t i=0; i < uart_rx_data_len - 1; i++){
      if (uart_rx_data[i+0] == UART_MESSAGE_PREAMBULE[0] && uart_rx_data[i+1] == UART_MESSAGE_PREAMBULE[1]){
        uart_rx_preambula_offset = i;
        break;
      }
    }
    if (uart_rx_preambula_offset) {
      analyze_uart_rx_trim(uart_rx_preambula_offset ); //обрезаем мусор до преамбулы
    }

    if (uart_rx_data_len >= 5){ //минимальная длина сообщения с преамбулой
      char* uart_rx_message = (char*)(uart_rx_data + 2);
      uint8_t length = uart_rx_message[0];
      if (length < 3 || length > UART_RX_BUF_LENGTH){
        //Serial1.println("invalid length");
        analyze_uart_rx_trim(2);  //пришел мусор, отрезаем преамбулу и надо пробовать искать преамбулу снова
        continue;
      }
          
      if (uart_rx_data_len >= length+2){    //в буфере данных уже столько, сколько описано в поле length
        if (check_crc(uart_rx_message, length - 1) == uart_rx_message[length - 1]){ //проверка crc
          //Serial1.println("crc ok");
          if (f){
            //Serial1.println("Uart message received: code: " + String((int)uart_rx_message[1]) + "; length: " + String(length - 3));
            f(uart_rx_message[1], &uart_rx_message[2], length - 3);
          }
          
          analyze_uart_rx_trim(length+2); //отрезаем прочитанное сообщение
          //Serial1.println("uart_rx_data_len: " + String(uart_rx_data_len));
        }else{
          //Serial1.println("crc error");
          analyze_uart_rx_trim(2); 
        }
      }else{
        //Serial1.println("not enough data: " + String(uart_rx_data_len));
        break;
      }
    }else{
      //Serial1.println("too short message yet (length<5)");
      break;
    }
  }
}

void loop() {
  while (Serial.available() > 0 && uart_rx_data_len < UART_RX_BUF_LENGTH) {
    uart_rx_data[uart_rx_data_len++] = Serial.read();
  }
  analyze_uart_rx(on_uart_message);
  
  ArduinoOTA.handle();
  yield();
}
