#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>


const char *ssid = "espNet";
const char *pass = "esp8266A";

IPAddress ip(192,168,1,121);  //Node static IP
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet);
  
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }
  ArduinoOTA.setHostname("meteo");
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
}
