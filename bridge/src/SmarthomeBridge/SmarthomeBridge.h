#ifndef SmarthomeBridge_h
#define SmarthomeBridge_h

#include "BoundedQueue.h"
using PacketQueue = BoundedQueue<clunet_packet*, 16>;

#define CLUNET_ID 0x80
#define CLUNET_DEVICE "SmarthomeBridge"

#define TIMEZONE TZ_Europe_Samara

#define LED_BLUE_PORT 5

typedef struct{
  uint32_t sequence;
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
  uint8_t refs;
};

struct api_request{
  bool info;
  char infoId[64];
  api_request_state* state;
  uint8_t address;
  uint8_t command;  
  int16_t responseFilterCommand;
  long responseTimeout;
  uint8_t size;
  char data[];

  unsigned char len(){return sizeof(api_request) + size;}
};

typedef struct{
  int requestId;
  int16_t expectedPayload;
  api_request_state* state;
  bool info;
  char infoId[64];
  int16_t responseFilterCommand;
} api_response;

#endif
