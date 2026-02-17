#ifndef BathFan_h
#define BathFan_h

//#include <vector>

#define CLUNET_DEVICE_ID 0x91
#define CLUNET_DEVICE_NAME "BathFan"

#define TIMEZONE TZ_Europe_Samara

#define BUTTON_PIN 14
#define BUTTON_TIMEOUT 50

#define MQTT_CLIENT_ID "bath-fan"
#define MQTT_TOPIC_DEVICE "home/" MQTT_CLIENT_ID
#define MQTT_TOPIC_STATUS MQTT_TOPIC_DEVICE "/status"
#define MQTT_TOPIC_SENSOR_SHT31 MQTT_TOPIC_DEVICE "/sensor/sht31"
#define MQTT_TOPIC_SHT31_STATE MQTT_TOPIC_SENSOR_SHT31 "/state"
#define MQTT_TOPIC_SHT31_META MQTT_TOPIC_SENSOR_SHT31 "/meta"
#define MQTT_TOPIC_HUMIDITY_CONTROLLER MQTT_TOPIC_DEVICE "/humidity-controller"

#endif
