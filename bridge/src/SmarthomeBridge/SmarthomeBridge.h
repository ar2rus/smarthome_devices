#ifndef SmarthomeBridge_h
#define SmarthomeBridge_h

#define CLUNET_ID 0x80
#define CLUNET_DEVICE "SmarthomeBridge"

#define TIMEZONE TZ_Europe_Samara

#define LED_BLUE_PORT 5

typedef struct{
  long timestamp;
  clunet_packet packet[];
} clunet_timestamp_packet;

typedef struct{
  long timestamp;
  clunet_response response[];
} clunet_timestamp_response;

#endif
