#ifndef HeatingController_h
#define HeatingController_h

#define MQTT_CLIENT_ID "heating-controller"

#define MQTT_TOPIC_DEVICE "home/" MQTT_CLIENT_ID
#define MQTT_TOPIC_STATUS MQTT_TOPIC_DEVICE "/status"

#define MQTT_TOPIC_SENSOR MQTT_TOPIC_DEVICE "/sensor"

#define TIMEZONE TZ_Europe_Samara

#define ONE_WIRE_PIN 4

#define ONE_WIRE_NUM_DEVICES 5
#define ONE_WIRE_UPDATE_PERIOD 1

#define DS18B20_NUM_REQUESTS (ONE_WIRE_NUM_DEVICES + 1)
#define DS18B20_REQUEST_PERIOD ((ONE_WIRE_UPDATE_PERIOD * 1000) / DS18B20_NUM_REQUESTS)

static const uint8_t DS18B20_DEVICES[ONE_WIRE_NUM_DEVICES][8] = {
  { 0x28, 0x61, 0x64, 0x35, 0xC8, 0x29, 0x68, 0x8F },
  { 0x28, 0x61, 0x64, 0x35, 0xC8, 0x15, 0x64, 0x4C },
  { 0x28, 0x61, 0x64, 0x35, 0xC8, 0x3D, 0x4A, 0xC7 },
  { 0x28, 0x61, 0x64, 0x35, 0xC9, 0xE4, 0x88, 0xF0 },
  { 0x28, 0x61, 0x64, 0x35, 0xC9, 0x9C, 0xE7, 0x14 }
};

static const char* DS18B20_DEVICES_LOCATIONS[ONE_WIRE_NUM_DEVICES] = {
  "channel_2_return",
  "channel_3_return",
  "input",
  "channel_4_return",
  "channel_1_return"
};

#define DS18B20_INPUT 2
#define DS18B20_CHANNEL_1 4
#define DS18B20_CHANNEL_2 0
#define DS18B20_CHANNEL_3 1
#define DS18B20_CHANNEL_4 3


#define RELAY_NUM 4

static const uint8_t RELAY_PIN[RELAY_NUM] = {13, 12, 14, 16};

#endif
