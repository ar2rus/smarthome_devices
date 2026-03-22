#ifndef SmarthomeBridge_h
#define SmarthomeBridge_h

class AsyncWebServerRequest;
struct clunet_packet;
struct clunet_response;

#define CLUNET_ID 0x80
#define CLUNET_DEVICE "SmarthomeBridge"

#define LED_BLUE_PORT 5
#define CLUNET_CONNECT_RETRY_INTERVAL_MS 5000UL

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
typedef struct api_request_state api_request_state;

struct api_request_state{
  AsyncWebServerRequest* webRequest;
  bool disconnected;
  uint8_t refs;
};

struct api_request{
  bool info;
  char infoId[64];
  api_request_state* requestState;
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
  api_request_state* requestState;
  bool info;
  char infoId[64];
  uint8_t responseFilterCommand;
} api_response;

#endif
