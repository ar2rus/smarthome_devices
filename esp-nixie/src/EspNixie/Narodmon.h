#ifndef NARODMON_h
#define NARODMON_h

#include <stdint.h>
#include <ESP8266WiFi.h>


#define NARODMON_URL "http://narodmon.ru/api"

#define NARODMON_TIMEOUT 5

#define JSON_BUFFER_SIZE 300

//минимальная задержка между запросами
#define MIN_REQUEST_PERIOD 1*60*1000

//время ожидания ответа
#define RESPONSE_TIMEOUT 10 * 1000

//максимальное время использования полученного
//значения температуры
#define T_MAX_TIME 60*60*1000

class Narodmon{
private:
    char config_uuid[32];
    char config_apiKey[16];
    uint8_t config_useLatLon;
    double config_lat;
    double config_lon;
    uint8_t config_radius = 0;

    //время получения последнего значения температуры
    uint32_t t_time = 0;
    //последнее полученное значение температуры
    int16_t t; 
    //время инициализации последнего запроса
    uint32_t request_time;

    uint8_t waiting_response = 0;
    WiFiClient client;
    
    void read_response();
    
public:
    Narodmon(char* device_id, char* api_key);

    void setConfigUseLatLon(uint8_t use);
    void setConfigLatLon(double lat, double lon);
    void setConfigRadius(uint8_t radius);
    void setApiKey(char* api_key);
    
    uint8_t request();
    uint8_t hasT();
    int16_t getT();
    void update();
};

#endif
