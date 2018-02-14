#include "Narodmon.h"

#include <ArduinoJson.h>
#include "JsonListener.h"

#include <MD5Builder.h>
#include <Arduino.h>

//#include "logging/Logging.h"

// Instantiate the shared instance that will be used.
LoggingClass Logging;

void LoggingClass::log(LoggingLevel level, const char *fmt, ...){

  //static const char *logLevelPrefixes[] = { "D", "I", "E"};

  //_stream.print(logLevelPrefixes[level]);
  //_stream.print(" ");

  String* s = NULL;
  switch (level){
    case LoggingLevelError:
      s = &_error;
      break;
    default:
      s = &_response;
      break;
  }

  if (s){
    *s += String(millis());
    *s += ": ";
    va_list fmtargs;
    va_start(fmtargs, fmt);
    char tmp[512];
    vsnprintf(tmp, sizeof(tmp), fmt, fmtargs);
    va_end(fmtargs);
    *s += tmp;
    *s += "\r\n";
  }
}


Narodmon::Narodmon(String device_id){
  MD5Builder md5;
  md5.begin();
  md5.add(device_id);
  md5.calculate();
  config_uuid = md5.toString();

  parser.setListener(this);
}

void Narodmon::setApiKey(String api_key){
  DEBUG("setApiKey: %s", api_key.c_str());
  config_apiKey = api_key;
}

void Narodmon::setConfigUseLatLng(uint8_t use){
  DEBUG("setConfigUseLatLng: %u", use);
  config_useLatLng = use;
}

void Narodmon::setConfigLatLng(double lat, double lng){
  DEBUG("setConfigLatLng: %f; %f", lat, lng);
  config_lat = lat;
  config_lng = lng;
  setConfigUseLatLng(1);
}

void Narodmon::setConfigRadius(uint8_t radius){
  DEBUG("setConfigRadius: %u", radius);
  config_radius = radius;
}

void Narodmon::setConfigReqT(uint8_t reqT){
  DEBUG("setConfigReqH: %u", reqT);
  config_reqT = reqT;
}

void Narodmon::setConfigReqH(uint8_t reqH){
  DEBUG("setConfigReqT: %u", reqH);
  config_reqH = reqH;
}

void Narodmon::setConfigReqP(uint8_t reqP){
  DEBUG("setConfigReqP: %u", reqP);
  config_reqP = reqP;
}
    
uint8_t Narodmon::request(){
  if (!aClient){
    RESET();
    DEBUG("New request");
   
    uint32_t tmp_t = millis();
    uint32_t delta_t = tmp_t - request_time;
    if ((request_time == 0) || (delta_t >= MIN_REQUEST_PERIOD)){

      aClient = new AsyncClient();
      if(!aClient){//could not allocate client
         ERROR("Couldn't allocate memory");
         return 0;
      }
      
      aClient->onError([this](void * arg, AsyncClient * client, int error){
        ERROR("Error %s (%i)", client->errorToString(error), error);
        
        //aClient = NULL;
        //delete client;
      }, NULL);

      aClient->onTimeout([this](void * arg, AsyncClient *client, uint32_t time) {
          ERROR("Timeout on client in %i", time);
          client->close();
      }, NULL);

      aClient->onDisconnect([this](void * arg, AsyncClient * client){
           DEBUG("Disconnected");
           
           aClient = NULL;
           delete client;
       }, NULL);

      aClient->onData([this](void * arg, AsyncClient * client, void * data, size_t len){
            DEBUG ("Got data: %i bytes", len);
            
            char * d = (char*)data;
            for(size_t i=0; i<len;i++){
              parser.parse(d[i]);
            }
      }, NULL);

      aClient->onConnect([this, tmp_t](void * arg, AsyncClient * client){
        DEBUG("Connected");
  
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
  
        DEBUG("Request: %s", json_post_data.c_str());

        values_cnt = 0;
        parser.reset();
        
        //send a request
        
        client->write("POST "); client->write(NARODMON_API_PATH); client->write(" HTTP/1.1\r\n");
        client->write("Host: "); client->write(NARODMON_HOST); client->write("\r\n");
        client->write("Content-Type: application/json\r\n");
        client->write("Cache-Control: no-cache\r\n");
        client->write("User-Agent: esp-nixie\r\n");

        client->write("Content-Length: "); client->write(String(json_post_data.length()).c_str()); client->write("\r\n");
        client->write("\r\n");
        client->write(json_post_data.c_str(), json_post_data.length());
        client->write("\r\n");

        request_time = tmp_t;

        aClient->onPoll([this](void * arg, AsyncClient * client){
            if (millis() - request_time > RESPONSE_TIMEOUT){
              ERROR("Poll timeout");
              client->close();
            }
        },NULL);
        
      }, NULL);

      if(!aClient->connect(NARODMON_HOST, NARODMON_PORT)){
        DEBUG("Connect fail");
        
        AsyncClient * client = aClient;
        aClient = NULL;
        delete client;
      }
    }else{
      DEBUG("Too short interval between requests. %u less then %u", delta_t, MIN_REQUEST_PERIOD);
    }
  }else{
    //DEBUG("Client is still working");
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
  DEBUG("End document");
  int16_t value = resolve_value(values, values_cnt, NARODMON_TYPE_TEMPERATURE, MIN);
  if (value != VALUE_NONE){
    t = value;
    t_time = millis();

    DEBUG("T=%f", (float)getT()/10);
  }

  value = resolve_value(values, values_cnt, NARODMON_TYPE_HUMIDITY, CLOSEST);
  if (value != VALUE_NONE){
    h = value;
    h_time = millis();

    DEBUG("H=%f", (float)getH()/10);
  }

  value = resolve_value(values, values_cnt, NARODMON_TYPE_PRESSURE, CLOSEST);
  if (value != VALUE_NONE){
    p = value;
    p_time = millis();

    DEBUG("P=%f", (float)getP()/10);
  }

  AsyncClient * client = aClient;
  if (client){
    client->close();
  }
}

void Narodmon::startArray() {
}

void Narodmon::startObject() {
  values[values_cnt].type = -1;
  values[values_cnt].value = VALUE_NONE;
  values[values_cnt].distance = d.distance;
}
