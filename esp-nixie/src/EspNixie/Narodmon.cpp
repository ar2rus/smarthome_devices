#include "Narodmon.h"

#include <ArduinoJson.h>
#include "JsonListener.h"

#include <MD5Builder.h>
#include "Arduino.h"

//#include "SerialDebug.h"

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
  response = String(millis())+";"+String(ESP.getFreeHeap())+";";
  if (!waiting_response){
    uint32_t tmp_t = millis();
    if ((request_time == 0) || ((tmp_t - request_time) >= MIN_REQUEST_PERIOD)){
      
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
        types.add(NARODMON_TYPE_TEMPERATURE); //temperature
      }
      if (config_reqH){
        types.add(NARODMON_TYPE_HUMIDITY); //humidity
      }
      if (config_reqP){
        types.add(NARODMON_TYPE_PRESSURE); //pressure
      }

      JSONRequest["limit"] = REQUEST_DEVICES_LIMIT;
      JSONRequest["pub"] = 1;
      JSONRequest["uuid"] = config_uuid;
      JSONRequest["api_key"] = config_apiKey;

      String json_post_data;
      JSONRequest.printTo(json_post_data);

      response += json_post_data + ";";

      #if DEBUG
        Serial.println("Request: " + json_post_data);
      #endif

      http.begin(NARODMON_HOST, 80, NARODMON_API_PATH);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("User-Agent", "esp-nixie");
      int httpCode = http.POST(json_post_data);

      //temp
      response += String(httpCode)+";";

     if (httpCode == HTTP_CODE_OK){
        response += "size="+String(http.getSize())+";";
      
        values_cnt = 0;
        parser.reset();
        
        waiting_response = 1;
        request_time = tmp_t;
        return 1;
     }else{
        //+= temp
        response += "HTTP response code = " + String(httpCode);
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

uint8_t Narodmon::hasH(){
  return h_time > 0 && (millis() - h_time) < H_MAX_TIME;
}

int16_t Narodmon::getH(){
  return h;
}

uint8_t Narodmon::hasP(){
  return p_time > 0 && (millis() - p_time) < P_MAX_TIME;
}

int16_t Narodmon::getP(){
  return p;
}



void Narodmon::update(){
  if (waiting_response){
    if (http.connected()){
      WiFiClient * stream = http.getStreamPtr();
      if(stream->available()){
        #if DEBUG
          Serial.println("Available bytes to parse: " + String(stream.available()));
        #endif
        
        //int yield_cnt = 0;
        while (stream->available()){
           char c = stream->read();
           parser.parse(c);
           yield();
           //if (++yield_cnt==100){
           //     //break;
           //     yield_cnt = 0;
           //     
           //}
        }
      }else{
        if (millis() - request_time > RESPONSE_TIMEOUT){
          http.end();
          waiting_response = 0;
          response = "Timeout while response reading";
          #if DEBUG
            Serial.println(response);
          #endif
        }
      }
    }else{
      http.end();
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
  parser_key = key;
}

void Narodmon::value(String value) {
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
  if (values[values_cnt].type >= 0 && values[values_cnt].value != VALUE_NONE){
    if (values_cnt < SENSOR_COUNT_LIMIT){
      values_cnt++;
      startObject();
    }
  }
}

int16_t resolve_value(sensor_value* values, int values_cnt, int type, RESOLVE_MODE resolve_mode){
  int32_t value = 0;

  int cnt = 0;
  for (int i=0; i < values_cnt; i++){
    if (values[i].type == type){
      switch (resolve_mode){
        case CLOSEST:
          if (cnt==0 || values[i].distance < values[value].distance){
            value = i;
          }
          break;
        case MIN:
          if (cnt==0 || values[i].value < value){
            value = values[i].value;
          }
          break;
        case MAX:
          if (cnt==0 || values[i].value > value){
            value = values[i].value;
          }
          break;
        case AVG:
          value += values[i].value;
          break;
      }
      cnt++;
    }
  }

  if (cnt > 0){
    switch (resolve_mode){
      case CLOSEST:
        value = values[value].value;
        break;
      case AVG:
        value /= cnt;
        break;
    }
    return value;
  }
  
  return VALUE_NONE;
}

void Narodmon::endDocument() {
  
  int16_t value = resolve_value(values, values_cnt, NARODMON_TYPE_TEMPERATURE, MIN);
  if (value != VALUE_NONE){
    t = value;
    t_time = millis();
    response += "OK(T="+String((float)getT()/10.0)+");";
    
    #if DEBUG
      Serial.print("T=" + String((float)getT()/10.0));
    #endif
  }

  value = resolve_value(values, values_cnt, NARODMON_TYPE_HUMIDITY, CLOSEST);
  if (value != VALUE_NONE){
    h = value;
    h_time = millis();
    response += "OK(H=" + String((float)getH()/10.0)+");";
    
    #if DEBUG
      Serial.print("H=" + String((float)getH()/10.0));
    #endif
  }

  value = resolve_value(values, values_cnt, NARODMON_TYPE_PRESSURE, CLOSEST);
  if (value != VALUE_NONE){
    p = value;
    p_time = millis();
    response += "OK(P=" + String((float)getP()/10.0)+");";
    
    #if DEBUG
      Serial.print("P=" + String((float)getP()/10.0));
    #endif
  }

  waiting_response = 0;
}

void Narodmon::startArray() {
}

void Narodmon::startObject() {
  values[values_cnt].type = -1;
  values[values_cnt].value = VALUE_NONE;
  values[values_cnt].distance = d.distance;
}
