#ifndef SmarthomeBridge_h
#define SmarthomeBridge_h

#define CLUNET_ID 0x80
#define CLUNET_DEVICE "SmarthomeBridge"

#define TIMEZONE TZ_Europe_Samara

#define LED_BLUE_PORT 5

typedef struct{
  uint32_t timestamp_sec;
  uint16_t timestamp_ms;
  clunet_packet packet[];
} ts_clunet_packet;

typedef struct{
  long timestamp;
  clunet_response response[];
} ts_clunet_response;

#endif
