#ifndef SmarthomeBridge_h
#define SmarthomeBridge_h

#define CLUNET_ID 0x80
#define CLUNET_DEVICE "SmarthomeBridge"

#define TIMEZONE TZ_Europe_Samara

#define HTTP_PORT 80

#define LED_BLUE_PORT 5

#define WIFI_CONNECT_TIMEOUT_MS 30000UL
#define WIFI_REBOOT_DELAY_MS 1500UL
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_SSID_PREFIX "SmarthomeBridge"
#define WIFI_CONNECT_RETRY_INTERVAL_MS 5000UL
#define WIFI_SETTINGS_MAGIC 0x534D4252UL

typedef struct{
  uint32_t timestamp_sec;
  uint16_t timestamp_ms;
  clunet_packet packet[];
} ts_clunet_packet;

typedef struct{
  long timestamp;
  clunet_response response[];
} ts_clunet_response;

typedef struct api_request api_request;

struct api_request{
  bool info;
  char infoId[64];
  AsyncWebServerRequest* webRequest;
  uint8_t address;
  uint8_t command;  
  uint8_t responseFilterCommand;
  long responseTimeout;
  uint8_t size;
  char data[];

  unsigned char len(){return sizeof(api_request) + size;}
};

typedef struct{
  int requestId;
  AsyncWebServerRequest* webRequest;
  bool info;
  char infoId[64];
  uint8_t responseFilterCommand;
} api_response;

#endif
