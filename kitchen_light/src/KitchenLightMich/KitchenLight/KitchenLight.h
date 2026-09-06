#ifndef KitchenLight_h
#define KitchenLight_h

#include <Arduino.h>
#include <TZ.h>

#define CLUNET_DEVICE_ID 0x89
#define CLUNET_DEVICE_NAME "KitchenLightNew"

#define TIMEZONE TZ_Europe_Samara

#define MQTT_CLIENT_ID "kitchen-light"

#define SETTINGS_STORAGE_MAGIC 0x4B4C4854UL
#define SETTINGS_STORAGE_VERSION 1

#define WIFI_CONNECT_TIMEOUT_MS 60000UL
#define WIFI_AP_RESTART_TIMEOUT_MS (15UL * 60UL * 1000UL)
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_SSID_PREFIX "KitchenLight"
#define CONFIG_REBOOT_DELAY_MS 1500UL
#define MQTT_RECONNECT_PERIOD_MS 5000UL

#define MQTT_TOPIC_DEVICE "home/" MQTT_CLIENT_ID
#define MQTT_TOPIC_STATUS MQTT_TOPIC_DEVICE "/status"
#define MQTT_TOPIC_BUTTON_EVENT MQTT_TOPIC_DEVICE "/button/event"
#define MQTT_TOPIC_LIGHT MQTT_TOPIC_DEVICE "/light"
#define MQTT_TOPIC_LIGHT_STATE MQTT_TOPIC_LIGHT "/state"
#define MQTT_TOPIC_LIGHT_META MQTT_TOPIC_LIGHT "/meta"
#define MQTT_TOPIC_LIGHT_SET MQTT_TOPIC_LIGHT "/set"
#define MQTT_TOPIC_LIGHT_SET_ON MQTT_TOPIC_LIGHT_SET "/on"
#define MQTT_TOPIC_LIGHT_SET_OFF MQTT_TOPIC_LIGHT_SET "/off"
#define MQTT_TOPIC_LIGHT_SET_TOGGLE MQTT_TOPIC_LIGHT_SET "/toggle"
#define MQTT_TOPIC_LIGHT_SET_BRIGHTNESS MQTT_TOPIC_LIGHT_SET "/brightness"
#define MQTT_LIGHT_EFFECT_DURATION_DEFAULT_MS 1000UL

#define RELAY_0_ID 1
#define BUTTON_ID 3

#define BUTTON_PIN 16
#define LIGHT_PIN 14


#define PWM_RANGE 1023
#define CLUNET_DIMMER_RANGE 255
#define PWM_FREQUENCY 100

#define DELAY_BEFORE_TOGGLE 25
#define DELAY_BEFORE_PWM 500
#define BUTTON_PWM_RESET_WINDOW 2000
#define BUTTON_PWM_TRAVEL_TIME 3000

#define PWM_DOWN_UP_CYCLE_TIME 4000
#define PWM_DOWN_UP_CYCLE_TIME_2 (int)(PWM_DOWN_UP_CYCLE_TIME/2)

enum WifiState {
  WIFI_STATE_IDLE,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_STA_CONNECTED,
  WIFI_STATE_AP_MODE
};

struct PersistedWifiSettings {
  char ssid[33];
  char password[65];
  uint8_t useStaticIp;
  char ip[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
};

struct PersistedMqttSettings {
  char host[64];
  uint16_t port;
  char user[33];
  char password[65];
};

struct PersistedSettings {
  uint32_t magic;
  uint16_t version;
  PersistedWifiSettings wifi;
  PersistedMqttSettings mqtt;
};

struct ButtonBrightnessSettings {
  uint16_t magic;
  uint8_t brightness;
};

#define BUTTON_BRIGHTNESS_SETTINGS_MAGIC 0x4B4C
#define BUTTON_BRIGHTNESS_EEPROM_OFFSET sizeof(PersistedSettings)
#define EEPROM_STORAGE_SIZE (sizeof(PersistedSettings) + sizeof(ButtonBrightnessSettings))

bool applyMqttBrightnessCommand(int brightness, unsigned long durationSeconds, const char* effectName, unsigned long effectDurationMs);

#endif
