#include "Narodmon.h"

#include <ArduinoJson.h>
#include "JsonStreamingParser.h"

#include <MD5Builder.h>
#include "Arduino.h"


Narodmon::Narodmon(char* device_id, char* api_key){
  MD5Builder md5;
  md5.begin();
  md5.add(device_id);
  md5.calculate();
  config_uuid = md5.toString();
  setApiKey(api_key);
}

void Narodmon::setApiKey(char* api_key){
  config_apiKey = String(api_key);
}

void Narodmon::setConfigUseLatLon(uint8_t use){
  config_useLatLon = use;
}

void Narodmon::setConfigLatLon(double lat, double lon){
  config_lat = lat;
  config_lon = lon;
  setConfigUseLatLon(1);
}

void Narodmon::setConfigRadius(uint8_t radius){
  config_radius = radius;
}

uint8_t Narodmon::request(){
  if (!waiting_response){
    uint32_t tmp_t = millis();
    if ((request_time ==0) || ((tmp_t - request_time) > MIN_REQUEST_PERIOD)){
      
      StaticJsonBuffer<JSON_BUFFER_SIZE> JSONbuffer;
      JsonObject& JSONRequest = JSONbuffer.createObject();
      
      JSONRequest["cmd"] = "sensorsNearby";
      if (config_useLatLon){
        JSONRequest["lat"] = config_lat;
        JSONRequest["lon"] = config_lon;
      }
      if (config_radius > 0){
        JSONRequest["radius"] = config_radius;
      }
      JsonArray& types = JSONRequest.createNestedArray("types");
      types.add(1); //temperature
      
      JSONRequest["pub"] = 1;
      JSONRequest["uuid"] = config_uuid;
      JSONRequest["api_key"] = config_apiKey;

      String json_post_data;
      JSONRequest.printTo(json_post_data);

     if (client.connect(NARODMON_HOST, 80)) {
        client.println(String("POST ") + NARODMON_API_PATH +" HTTP/1.1");
        client.println(String("Host: ") + NARODMON_HOST);
        client.println("Cache-Control: no-cache");
        client.println("User-Agent: esp-nixie");
        client.println("Content-Type: application/json");
        client.print("Content-Length: ");
        client.println(json_post_data.length());
        client.println();
        client.println(json_post_data);

        waiting_response = 1;
        request_time = tmp_t;
        return 1;
      }else{
        response = "conn timeout";
      }
    }else{
      response = "too short req time";
    }
  }
  
  return 0;
}

uint8_t Narodmon::hasT(){
  return t_time > 0 && (millis() - t_time) < T_MAX_TIME;
}

int16_t Narodmon::getT(){
  return t;
}

void Narodmon::update(){
  if (waiting_response){
    if (client.connected()){
      if(client.available()){
        while (client.available()){
          response+=client.readStringUntil('\r')+'\r';
        }
        //response = client.readStringUntil('\r');
        //while (client.available()){
        //  ;
        //}
        waiting_response = 0;
      }else{
        if (millis() - request_time > RESPONSE_TIMEOUT){
          client.stop();
          waiting_response = 0;
          response = "timeout";
        }
      }
    }else{
      client.stop();
      waiting_response = 0;
      response = "error";
    }
  }
}

