#ifndef RelayController_h
#define RelayController_h

#include <vector>
#include "heatfloor.h"

#define CLUNET_DEVICE_ID 0x90
#define CLUNET_DEVICE_NAME "RelayController"

#define TIMEZONE TZ_Europe_Samara

#define BUTTON_PIN 2
#define BUTTON_TIMEOUT 50

#define ONE_WIRE_SUPPLY_PIN 16

#define ONE_WIRE_PIN 14

#define ONE_WIRE_NUM_DEVICES 7
#define ONE_WIRE_UPDATE_PERIOD 30

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


#define RELAY_NUM 5

// Индексы реле
#define RELAY_BATHROOM_FAN 0
#define RELAY_TOILET_FAN 1
#define RELAY_BATHROOM_HEATWALL 2
#define RELAY_BATHROOM_HEATFLOOR 3
#define RELAY_TOILET_HEATFLOOR 4

static const uint8_t RELAY_PIN[RELAY_NUM] = {13, 12, 5, 4, 15};

// Определение для каналов нагрева
#define HEATING_CHANNELS_NUM 3
#define HEATING_BATHROOM_FLOOR_CHANNEL 0
#define HEATING_BATHROOM_WALL_CHANNEL 1 
#define HEATING_TOILET_FLOOR_CHANNEL 2

// Определение для каналов вентиляторов
#define FAN_CHANNELS_NUM 2
#define FAN_TOILET_CHANNEL 0
#define FAN_BATHROOM_CHANNEL 1

// Структура для хранения конфигурации канала вентилятора
struct FanChannelConfig {
  const char* name;                 // Имя канала для использования в JSON
  const char* displayName;          // Отображаемое имя
  uint8_t relayPin;                 // Индекс реле
  unsigned long defaultTimerMinutes; // Время таймера по умолчанию в минутах
};

// Статическая конфигурация каналов вентиляторов
static const FanChannelConfig FAN_CHANNELS_CONFIG[FAN_CHANNELS_NUM] = {
  {
    "toiletFan",
    "Вентилятор туалета",
    RELAY_TOILET_FAN,
    30  // 30 минут по умолчанию
  },
  {
    "bathroomFan",
    "Вентилятор ванной",
    RELAY_BATHROOM_FAN,
    30  // 30 минут по умолчанию
  }
};

// Структура для хранения конфигурации канала нагрева
struct HeatingChannelConfig {
  const char* name;           // Имя канала для использования в JSON
  const char* displayName;    // Отображаемое имя
  uint8_t temperatureSensor;  // Индекс датчика температуры
  uint8_t relayPin;           // Индекс реле
  const FloorHeatingSchedule* defaultSchedule; // Указатель на массив дефолтного расписания
  uint8_t defaultScheduleSize; // Размер массива дефолтного расписания
};

// Дефолтные расписания для контроллеров теплого пола
static const FloorHeatingSchedule DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE[] = {
  {6, 30, 30.0, -2},
  {7, 30, 30.0, -3},
  {9, 30, 28.0, -1},
  {19, 0, 30.0, -1},
  {22, 30, 28.0, -1}
};
static const int DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE_SIZE = sizeof(DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE) / sizeof(DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE[0]);

static const FloorHeatingSchedule DEFAULT_BATHROOM_HEATWALL_SCHEDULE[] = {
  {0, 0, 30, -1},
  {10, 0, 28, -2},
  {10, 0, 30, -3},
  {17, 0, 30, -2},
  {18, 30, 31, -1}
};
static const int DEFAULT_BATHROOM_HEATWALL_SCHEDULE_SIZE = sizeof(DEFAULT_BATHROOM_HEATWALL_SCHEDULE) / sizeof(DEFAULT_BATHROOM_HEATWALL_SCHEDULE[0]);

static const FloorHeatingSchedule DEFAULT_TOILET_HEATFLOOR_SCHEDULE[] = {
  {6, 0, 28.5, -2},
  {9, 30, 27, -2},
  {17, 0, 28.5, -2},
  {8, 0, 28.5, -3},
  {10, 30, 28, -3}
};
static const int DEFAULT_TOILET_HEATFLOOR_SCHEDULE_SIZE = sizeof(DEFAULT_TOILET_HEATFLOOR_SCHEDULE) / sizeof(DEFAULT_TOILET_HEATFLOOR_SCHEDULE[0]);

// Конфигурация каналов нагрева
static const HeatingChannelConfig HEATING_CHANNELS_CONFIG[HEATING_CHANNELS_NUM] = {
  {
    "bathroomFloor",
    "Теплый пол ванной",
    DS18B20_BATHROOM_HEATFLOOR,
    RELAY_BATHROOM_HEATFLOOR,
    DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE,
    DEFAULT_BATHROOM_HEATFLOOR_SCHEDULE_SIZE
  },
  {
    "bathroomWall",
    "Теплая стена ванной",
    DS18B20_BATHROOM_HEATWALL,
    RELAY_BATHROOM_HEATWALL,
    DEFAULT_BATHROOM_HEATWALL_SCHEDULE,
    DEFAULT_BATHROOM_HEATWALL_SCHEDULE_SIZE
  },
  {
    "toiletFloor",
    "Теплый пол туалета",
    DS18B20_TOILET_HEATFLOOR,
    RELAY_TOILET_HEATFLOOR,
    DEFAULT_TOILET_HEATFLOOR_SCHEDULE,
    DEFAULT_TOILET_HEATFLOOR_SCHEDULE_SIZE
  }
};

// Функции для работы с настройками теплого пола
void loadHeatfloorSettingsFromFile();
void saveHeatfloorSettingsToFile();

#endif
