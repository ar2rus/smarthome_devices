#include "Narodmon.h"

#include <ArduinoJson.h>
#include "JsonListener.h"

#include <MD5Builder.h>
#include "Arduino.h"

#include "SerialDebug.h"

Narodmon::Narodmon(String device_id){
  MD5Builder md5;
  md5.begin();
  md5.add(device_id);
  md5.calculate();
  config_uuid = md5.toString();

  parser.setListener(this);
}

void Narodmon::setApiKey(String api_key){
  #if DEBUG
  Serial.println("setApiKey: " + api_key);
  #endif
  config_apiKey = api_key;
}

void Narodmon::setConfigUseLatLng(uint8_t use){
  #if DEBUG
  Serial.println("setConfigUseLatLng: " + String(use));
  #endif
  config_useLatLng = use;
}

void Narodmon::setConfigLatLng(double lat, double lng){
  #if DEBUG
  Serial.println("setConfigLatLng: " + String(lat) + ";" + String(lng));
  #endif
  config_lat = lat;
  config_lng = lng;
  setConfigUseLatLng(1);
}

void Narodmon::setConfigRadius(uint8_t radius){
  #if DEBUG
  Serial.println("setConfigRadius: " + String(radius));
  #endif
  config_radius = radius;
}

void Narodmon::setConfigReqT(uint8_t reqT){
  #if DEBUG
  Serial.println("setConfigReqH: " + String(reqT));
  #endif
  config_reqT = reqT;
}

void Narodmon::setConfigReqH(uint8_t reqH){
  #if DEBUG
  Serial.println("setConfigReqT: " + String(reqH));
  #endif
  config_reqH = reqH;
}

void Narodmon::setConfigReqP(uint8_t reqP){
  #if DEBUG
  Serial.println("setConfigReqP: " + String(reqP));
  #endif
  config_reqP = reqP;
}
    

uint8_t Narodmon::request(){
  response = String(ESP.getFreeHeap())+";";
  if (!waiting_response){
    uint32_t tmp_t = millis();
    if ((request_time ==0) || ((tmp_t - request_time) > MIN_REQUEST_PERIOD)){
      
      StaticJsonBuffer<JSON_BUFFER_SIZE> JSONbuffer;
      JsonObject& JSONRequest = JSONbuffer.createObject();
      
      JSONRequest["cmd"] = "sensorsNearby";
      if (config_useLatLng){
        JSONRequest["lat"] = config_lat;
        JSONRequest["lng"] = config_lng;
      }
      if (config_radius > 0){
        JSONRequest["radius"] = config_radius;
      }
      JsonArray& types = JSONRequest.createNestedArray("types");
      if (config_reqT){
        types.add(1); //temperature
      }
      if (config_reqH){
        types.add(2); //humidity
      }
      if (config_reqP){
        types.add(3); //pressure
      }

      JSONRequest["limit"] = REQUEST_DEVICES_LIMIT;
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

        #if DEBUG
        Serial.println("Request: " + json_post_data);
        #endif

        values_cnt = 0;
        parser.reset();
        
        waiting_response = 1;
        request_time = tmp_t;
        return 1;
     }else{
        response = "Can't connect to " + String(NARODMON_HOST);
        #if DEBUG
        Serial.println(response);
        #endif
     }
    }else{
      response = "Too short interval between requests";
      #if DEBUG
      Serial.println(response);
      #endif
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

uint8_t Narodmon::hasP(){
  return p_time > 0 && (millis() - p_time) < P_MAX_TIME;
}

int16_t Narodmon::getP(){
  return p;
}

void Narodmon::update(){
  if (waiting_response){
    if (client.connected()){
      if(client.available()){
        #if DEBUG
        Serial.println("Available bytes to parse: " + String(client.available()));
        #endif
        while (client.available()){
           char c = client.read();
           parser.parse(c);
        }
      }else{
        if (millis() - request_time > RESPONSE_TIMEOUT){
          client.stop();
          waiting_response = 0;
          response = "Timeout in response reading";
          #if DEBUG
          Serial.println(response);
          #endif
        }
      }
    }else{
      client.stop();
      waiting_response = 0;
      response = "No connection to server";
      #if DEBUG
      Serial.println(response);
      #endif
    }
  }
}

void Narodmon::whitespace(char c) {
}

void Narodmon::startDocument() {
}

void Narodmon::key(String key) {
  //Serial.print(key + "=");
  parser_key = key;
}

void Narodmon::value(String value) {
  //Serial.println(value);
  if (parser_key.equals("value")){
    values[values_cnt].value = (int)(value.toFloat()*10);
  }else if (parser_key.equals("type")){
    values[values_cnt].type = value.toInt();
  }else if (parser_key.equals("distance")){
    d.distance = (int)(value.toFloat()*100);
  }
  parser_key = "";
}

void Narodmon::endArray() {
}

void Narodmon::endObject() {
  if (values[values_cnt].type >= 0 && values[values_cnt].value != INT16_MAX){
    if (values_cnt < SENSOR_COUNT_LIMIT){
      values_cnt++;
      startObject();
    }
  }
}

void Narodmon::endDocument() {
  t = INT16_MAX;
  p = INT16_MAX;
  for (int i=0; i < values_cnt; i++){
    switch (values[i].type){
      case 1: //температуру берем самую низкую
        #if DEBUG
        Serial.print("t [val="+String(values[i].value)+"];");
        #endif
        if (values[i].value < t){
          t = values[i].value;
        }
        break;
      case 3: //давление берем с ближайшего датчика
        #if DEBUG
        Serial.print("p [dist="+String(values[i].distance)+"; val="+String(values[i].value)+"];");
        #endif
        if (p==INT16_MAX || values[i].distance < values[p].distance){
          p = i;
        }
        break;
    }
  }

  boolean r = false;
  if (t < INT16_MAX){
    t_time = millis();
    #if DEBUG
    Serial.print("T=" + String((float)getT()/10.0));
    r = true;
    #endif
    response += "OK(T="+String((float)getT()/10.0)+");";
  }

  if (p < INT16_MAX){
    p = values[p].value;
    p_time = millis();
    #if DEBUG
    Serial.print("P=" + String((float)getP()/10.0));
    r = true;
    #endif
    response += "OK(P=" + String((float)getP()/10.0)+");";
  }

  #if DEBUG
  if (r){
    Serial.println();
  }
  #endif

  waiting_response = 0;
}

void Narodmon::startArray() {
}

void Narodmon::startObject() {
  values[values_cnt].type = -1;
  values[values_cnt].value = INT16_MAX;
  values[values_cnt].distance = d.distance;
}
