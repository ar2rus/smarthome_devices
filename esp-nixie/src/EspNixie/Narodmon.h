#ifndef NARODMON_h
#define NARODMON_h

#include <stdint.h>
#include <ESP8266WiFi.h>

#include "JsonStreamingParser.h"
#include "JsonListener.h"

#define NARODMON_HOST "narodmon.ru"
#define NARODMON_API_PATH "/api"


#define NARODMON_TIMEOUT 5

#define JSON_BUFFER_SIZE 300

//минимальная задержка между запросами
#define MIN_REQUEST_PERIOD 1*60*1000

//время ожидания ответа
#define RESPONSE_TIMEOUT 10 * 1000

//максимальное время использования полученного
//значения температуры
#define T_MAX_TIME 60*60*1000

class Narodmon: public JsonListener{
private:
    String config_uuid;
    String config_apiKey;
    uint8_t config_useLatLon;
    double config_lat;
    double config_lon;
    uint8_t config_radius = 0;

    //время получения последнего значения температуры
    uint32_t t_time = 0;
    //последнее полученное значение температуры
    int16_t t;
    //время инициализации последнего запроса
    uint32_t request_time = 0;

    uint8_t waiting_response = 0;
    
    WiFiClient client;
    JsonStreamingParser parser;
    String parser_key;
    
public:
    String response;
    Narodmon(String device_id, String api_key);

    void setConfigUseLatLon(uint8_t use);
    void setConfigLatLon(double lat, double lon);
    void setConfigRadius(uint8_t radius);
    void setApiKey(String api_key);
    
    uint8_t request();
    uint8_t hasT();
    int16_t getT();
    void update();

    virtual void whitespace(char c);
    virtual void startDocument();
    virtual void key(String key);
    virtual void value(String value);
    virtual void endArray();
    virtual void endObject();
    virtual void endDocument();
    virtual void startArray();
    virtual void startObject();
};

#endif
