#ifndef BathFan_h
#define BathFan_h

//#include <vector>

#define TIMEZONE TZ_Europe_Samara

#define BUTTON_PIN 14
#define BUTTON_TIMEOUT 50

#define MQTT_CLIENT_ID "bath-fan"

#define MQTT_TOPIC_DEVICE "home/" MQTT_CLIENT_ID
#define MQTT_TOPIC_DEVICE_META MQTT_TOPIC_DEVICE "/meta"
#define MQTT_TOPIC_STATUS MQTT_TOPIC_DEVICE "/status"

#define MQTT_TOPIC_SENSOR_SHT31 MQTT_TOPIC_DEVICE "/sensor/sht31"
#define MQTT_TOPIC_SHT31_META MQTT_TOPIC_SENSOR_SHT31 "/meta"
#define MQTT_TOPIC_SHT31_STATE MQTT_TOPIC_SENSOR_SHT31 "/state"

#define MQTT_TOPIC_HUMIDITY_CONTROLLER MQTT_TOPIC_DEVICE "/humidity-controller"
#define MQTT_TOPIC_HUMIDITY_CONTROLLER_STATE MQTT_TOPIC_HUMIDITY_CONTROLLER "/state"
#define MQTT_TOPIC_HUMIDITY_CONTROLLER_EVENT MQTT_TOPIC_HUMIDITY_CONTROLLER "/event"

#define MQTT_TOPIC_BUTTON MQTT_TOPIC_DEVICE "/button"
#define MQTT_TOPIC_BUTTON_EVENT MQTT_TOPIC_BUTTON "/event"

#define MQTT_TOPIC_FAN_BATHROOM_ON "home/relay-controller/fan-controller/fan-bathroom/set/on"
#define MQTT_TOPIC_FAN_BATHROOM_TOGGLE "home/relay-controller/fan-controller/fan-bathroom/set/toggle"

#endif
