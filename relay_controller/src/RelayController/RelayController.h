#ifndef RelayController_h
#define RelayController_h

#include <Arduino.h>
#include <time.h>
#include <vector>
#include "Thermostat.h"
#include "TimeZones.h"

#define MQTT_CLIENT_ID "relay-controller"

#define SETTINGS_STORAGE_MAGIC 0x52434F4EUL
#define SETTINGS_STORAGE_VERSION 1

#define WIFI_CONNECT_TIMEOUT_MS 60000UL
#define WIFI_CONNECT_RETRY_INTERVAL_MS 5000UL
#define WIFI_AP_RESTART_TIMEOUT_MS (15UL * 60UL * 1000UL)
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_SSID_PREFIX "RelayController"
#define CONFIG_REBOOT_DELAY_MS 1500UL

#define MQTT_TOPIC_DEVICE "home/" MQTT_CLIENT_ID
#define MQTT_TOPIC_STATUS MQTT_TOPIC_DEVICE "/status"
#define MQTT_TOPIC_BUTTON_EVENT MQTT_TOPIC_DEVICE "/button/event"

#define MQTT_TOPIC_SENSOR MQTT_TOPIC_DEVICE "/sensor"

#define MQTT_TOPIC_RELAY MQTT_TOPIC_DEVICE "/relay"

#define MQTT_TOPIC_THERMOSTAT MQTT_TOPIC_DEVICE "/thermostat"

#define MQTT_TOPIC_ONEWIRE MQTT_TOPIC_DEVICE "/onewire"
#define MQTT_TOPIC_ONEWIRE_STATE MQTT_TOPIC_ONEWIRE "/state"
#define MQTT_TOPIC_ONEWIRE_SCAN MQTT_TOPIC_ONEWIRE "/scan"
#define MQTT_TOPIC_ONEWIRE_SET MQTT_TOPIC_ONEWIRE "/set"
#define MQTT_TOPIC_ONEWIRE_SET_ON MQTT_TOPIC_ONEWIRE_SET "/on"
#define MQTT_TOPIC_ONEWIRE_SET_OFF MQTT_TOPIC_ONEWIRE_SET "/off"

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

struct DS18B20Reading {
  float temperature;
  time_t timestamp;
  unsigned long lastReadMs;
  bool hasRecentReading;

  bool hasValue() const {
    return hasRecentReading || timestamp > 0;
  }

  bool hasActualValue() const {
    static const unsigned long ACTUAL_VALUE_MAX_AGE_MS = 30UL * 60UL * 1000UL;
    if (!hasRecentReading) {
      return false;
    }
    return static_cast<unsigned long>(millis() - lastReadMs) <= ACTUAL_VALUE_MAX_AGE_MS;
  }
};


#define BUTTON_PIN 2
#define BUTTON_TIMEOUT 50

#define ONE_WIRE_SUPPLY_PIN 16

#define ONE_WIRE_PIN 14

#define ONE_WIRE_NUM_DEVICES 7
#define ONE_WIRE_UPDATE_PERIOD 10

#define DS18B20_NUM_REQUESTS (ONE_WIRE_NUM_DEVICES + 1)
#define DS18B20_REQUEST_PERIOD ((ONE_WIRE_UPDATE_PERIOD * 1000) / DS18B20_NUM_REQUESTS)

static const uint8_t DS18B20_DEVICES[ONE_WIRE_NUM_DEVICES][8] = {
  { 0x28, 0x61, 0x64, 0x34, 0x2B, 0x70, 0xC4, 0xEA },
  { 0x28, 0x83, 0xA5, 0x56, 0xB5, 0x01, 0x3C, 0xCD },
  { 0x28, 0xD0, 0x77, 0x56, 0xB5, 0x01, 0x3C, 0x67 },
  { 0x28, 0xB8, 0x21, 0x56, 0xB5, 0x01, 0x3C, 0x0C },
  { 0x28, 0x61, 0x64, 0x34, 0x29, 0x4E, 0xF5, 0xB4 },
  { 0x28, 0x61, 0x64, 0x34, 0x2B, 0xFE, 0x6D, 0x2A },
  { 0x28, 0x61, 0x64, 0x34, 0x28, 0x0D, 0x42, 0x60 }
};

#define DS18B20_TOILET_HEATFLOOR 0
#define DS18B20_BATHROOM_HEATFLOOR 1
#define DS18B20_BATHROOM_HEATWALL 2
#define DS18B20_BALCONY 3
#define DS18B20_BEDROOM 4
#define DS18B20_CHILDROOM 5
#define DS18B20_LIVINGROOM 6

static const char* DS18B20_DEVICES_LOCATIONS[ONE_WIRE_NUM_DEVICES] = {
  "toilet_heatfloor",
  "bathroom_heatfloor",
  "bathroom_heatwall",
  "balcony",
  "bedroom",
  "childroom",
  "livingroom"
};


#define RELAY_NUM 5

// Индексы реле
#define RELAY_BATHROOM_FAN 0
#define RELAY_TOILET_FAN 1
#define RELAY_BATHROOM_HEATWALL 2
#define RELAY_BATHROOM_HEATFLOOR 3
#define RELAY_TOILET_HEATFLOOR 4

static const uint8_t RELAY_PIN[RELAY_NUM] = {13, 12, 5, 4, 15};

static const uint8_t SHIFT_REGISTER_DATA_PIN = 1;
static const uint8_t SHIFT_REGISTER_LATCH_PIN = 3;
static const uint8_t SHIFT_REGISTER_CLOCK_PIN = 0;

static const uint8_t SHIFT_REGISTER_WIFI_LED_BIT = 0;
static const uint8_t SHIFT_REGISTER_ONEWIRE_LED_BIT = 1;

// Определение для каналов нагрева
#define THERMOSTAT_CHANNELS_NUM 3
#define THERMOSTAT_BATHROOM_FLOOR_CHANNEL 0
#define THERMOSTAT_BATHROOM_WALL_CHANNEL 1 
#define THERMOSTAT_TOILET_FLOOR_CHANNEL 2

static const uint8_t SHIFT_REGISTER_THERMOSTAT_LED_BITS[THERMOSTAT_CHANNELS_NUM] = {2, 3, 4};

// Определение для каналов вентиляторов
#define RELAY_CHANNELS_NUM 2
#define RELAY_TOILET_CHANNEL 0
#define RELAY_BATHROOM_CHANNEL 1

// Структура для хранения конфигурации канала вентилятора
struct RelayChannelConfig {
  const char* name;                 // Имя канала для использования в JSON
  const char* topicName;            // Имя канала для MQTT топиков
  const char* location;             // Локация вентилятора
  const char* displayName;          // Отображаемое имя
  uint8_t relayPin;                 // Индекс реле
  unsigned long defaultTimerMinutes; // Время таймера по умолчанию в минутах
};

// Статическая конфигурация каналов вентиляторов
static const RelayChannelConfig RELAY_CHANNELS_CONFIG[RELAY_CHANNELS_NUM] = {
  {
    "toiletFan",
    "fan-toilet",
    "toilet",
    "Вентилятор туалета",
    RELAY_TOILET_FAN,
    30  // 30 минут по умолчанию
  },
  {
    "bathroomFan",
    "fan-bathroom",
    "bathroom",
    "Вентилятор ванной",
    RELAY_BATHROOM_FAN,
    30  // 30 минут по умолчанию
  }
};

// Структура для хранения конфигурации канала нагрева
struct ThermostatChannelConfig {
  const char* name;           // Имя канала для использования в JSON
  const char* topicName;      // Имя канала для MQTT топиков
  const char* location;       // Локация
  const char* displayName;    // Отображаемое имя
  uint8_t temperatureSensor;  // Индекс датчика температуры
  uint8_t relayPin;           // Индекс реле
  const ThermostatSchedule* defaultSchedule; // Указатель на массив дефолтного расписания
  uint8_t defaultScheduleSize; // Размер массива дефолтного расписания
};

// Дефолтные расписания для контроллеров теплого пола
static const ThermostatSchedule DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE[] = {
  {6, 30, 30.0, -2},
  {7, 30, 30.0, -3},
  {9, 30, 28.0, -1},
  {19, 0, 30.0, -1},
  {22, 30, 28.0, -1}
};
static const int DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE_SIZE = sizeof(DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE) / sizeof(DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE[0]);

static const ThermostatSchedule DEFAULT_BATHROOM_HEATWALL_SCHEDULE[] = {
  {0, 0, 30, -1},
  {10, 0, 28, -2},
  {10, 0, 30, -3},
  {17, 0, 30, -2},
  {18, 30, 31, -1}
};
static const int DEFAULT_BATHROOM_HEATWALL_SCHEDULE_SIZE = sizeof(DEFAULT_BATHROOM_HEATWALL_SCHEDULE) / sizeof(DEFAULT_BATHROOM_HEATWALL_SCHEDULE[0]);

static const ThermostatSchedule DEFAULT_TOILET_HEATFLOOR_SCHEDULE[] = {
  {6, 0, 28.5, -2},
  {9, 30, 27, -2},
  {17, 0, 28.5, -2},
  {8, 0, 28.5, -3},
  {10, 30, 28, -3}
};
static const int DEFAULT_TOILET_HEATFLOOR_SCHEDULE_SIZE = sizeof(DEFAULT_TOILET_HEATFLOOR_SCHEDULE) / sizeof(DEFAULT_TOILET_HEATFLOOR_SCHEDULE[0]);

// Конфигурация каналов нагрева
static const ThermostatChannelConfig THERMOSTAT_CHANNELS_CONFIG[THERMOSTAT_CHANNELS_NUM] = {
  {
    "bathroomFloor",
    "heating-floor-bathroom",
    "bathroom_floor",
    "Теплый пол ванной",
    DS18B20_BATHROOM_HEATFLOOR,
    RELAY_BATHROOM_HEATFLOOR,
    DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE,
    DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE_SIZE
  },
  {
    "bathroomWall",
    "heating-wall-bathroom",
    "bathroom_wall",
    "Теплая стена ванной",
    DS18B20_BATHROOM_HEATWALL,
    RELAY_BATHROOM_HEATWALL,
    DEFAULT_BATHROOM_HEATWALL_SCHEDULE,
    DEFAULT_BATHROOM_HEATWALL_SCHEDULE_SIZE
  },
  {
    "toiletFloor",
    "heating-floor-toilet",
    "toilet_floor",
    "Теплый пол туалета",
    DS18B20_TOILET_HEATFLOOR,
    RELAY_TOILET_HEATFLOOR,
    DEFAULT_TOILET_HEATFLOOR_SCHEDULE,
    DEFAULT_TOILET_HEATFLOOR_SCHEDULE_SIZE
  }
};

// Функции для работы с настройками теплого пола
void loadThermostatSettingsFromFile();
void saveThermostatSettingsFromFile();

#endif
