/**
    Use 3.1.2 esp8266 core
    lwip v2 Higher bandwidth; CPU 80 MHz
    4M (FS: 1Mb OTA:~1019Kb)

    dependencies:
    https://github.com/me-no-dev/ESPAsyncWebServer
    AsyncMqttClient

 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include "RelayController.h"
#include "Config.h"
#include "MQTT.h"

#include "Thermostat.h"
#include "Relay.h"
#include "OneWireWatchdog.h"

#include <ESPInputs.h>

#include <ArduinoJson.h>

#include <time.h>
#include <LittleFS.h>

#include <vector>

Inputs inputs;

// Массивы для настроек и контроллеров теплого пола
ThermostatSettings thermostatSettings[THERMOSTAT_CHANNELS_NUM];
Thermostat* thermostats[THERMOSTAT_CHANNELS_NUM];

// Массив контроллеров вентиляторов
Relay* relays[RELAY_CHANNELS_NUM];

bool oneWirePowerEnabled = false;
bool ds18b20NeedsRequest = true;
void applyShiftRegisterState();
void setShiftRegisterLedBit(uint8_t* value, uint8_t bitIndex, bool on);
void updateStatusLeds(unsigned long nowMs);
bool isOneWireAlive();

void applyOneWireEnable(bool enabled){
  if (oneWirePowerEnabled != enabled) {
    oneWirePowerEnabled = enabled;
    digitalWrite(ONE_WIRE_SUPPLY_PIN, enabled ? HIGH : LOW);

    unsigned long nowMs = millis();
    if (enabled) {
      // After power restore the sensors need a fresh conversion request.
      ds18b20NeedsRequest = true;
    }

    if (mqttClient.connected()) {
      publishMqttOneWireState(enabled);
    }
    updateStatusLeds(nowMs);
  }
}

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature DS18B20(&oneWire);

DS18B20Reading DS18B20_values[ONE_WIRE_NUM_DEVICES];

bool relay_states[RELAY_NUM];

void relay_state(int index, bool _on){
  if (relay_states[index] != _on){
     relay_states[index] = _on;
     apply_relay_state(index);
     updateStatusLeds(millis());
  }
}

void apply_relay_state(int index){
  digitalWrite(RELAY_PIN[index], relay_states[index] ? HIGH : LOW);
}

AsyncWebServer server(80); // Порт 80
bool littleFsAvailable = false;

uint8_t shiftRegisterState = 0xFF;
unsigned long lastShiftRegisterRefreshMs = 0;
bool oneWireWatchdogRestartInProgress = false;

static const unsigned long ONE_WIRE_WATCHDOG_RESET_OFF_MS = 30UL * 1000UL;
static const unsigned long ONE_WIRE_WATCHDOG_GRACE_MS = 2 * ONE_WIRE_UPDATE_PERIOD * 1000UL;
static const unsigned long MQTT_RECONNECT_PERIOD_MS = 5000UL;
static const unsigned long WIFI_LED_BLINK_PERIOD_MS = 500UL;
static const unsigned long SHIFT_REGISTER_REFRESH_PERIOD_MS = 1000UL;

unsigned long lastMqttReconnect = 0;

OneWireWatchdog oneWireWatchdog(
  ONE_WIRE_WATCHDOG_GRACE_MS,
  ONE_WIRE_WATCHDOG_RESET_OFF_MS,
  [](bool enabled) {
    if (!enabled) {
      oneWireWatchdogRestartInProgress = true;
    } else if (oneWireWatchdogRestartInProgress) {
      oneWireWatchdogRestartInProgress = false;
    }
    applyOneWireEnable(enabled);
  }
);

void applyShiftRegisterState() {
  digitalWrite(SHIFT_REGISTER_LATCH_PIN, LOW);
  shiftOut(SHIFT_REGISTER_DATA_PIN, SHIFT_REGISTER_CLOCK_PIN, MSBFIRST, shiftRegisterState);
  digitalWrite(SHIFT_REGISTER_LATCH_PIN, HIGH);
  lastShiftRegisterRefreshMs = millis();
}

void setShiftRegisterLedBit(uint8_t* value, uint8_t bitIndex, bool on) {
  if (value == nullptr) {
    return;
  }

  if (on) {
    *value &= static_cast<uint8_t>(~(1U << bitIndex));
  } else {
    *value |= static_cast<uint8_t>(1U << bitIndex);
  }
}

void updateStatusLeds(unsigned long nowMs) {
  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  bool wifiLedOn = wifiConnected || (((nowMs / WIFI_LED_BLINK_PERIOD_MS) % 2U) == 0U);
  bool oneWireLedOn = false;

  if (oneWireWatchdogRestartInProgress) {
    oneWireLedOn = (((nowMs / WIFI_LED_BLINK_PERIOD_MS) % 2U) == 0U);
  } else if (oneWirePowerEnabled) {
    oneWireLedOn = isOneWireAlive();
  }

  uint8_t nextState = 0xFF;
  setShiftRegisterLedBit(&nextState, SHIFT_REGISTER_WIFI_LED_BIT, wifiLedOn);
  setShiftRegisterLedBit(&nextState, SHIFT_REGISTER_ONEWIRE_LED_BIT, oneWireLedOn);
  for (uint8_t i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
    setShiftRegisterLedBit(&nextState, SHIFT_REGISTER_THERMOSTAT_LED_BITS[i], relay_states[THERMOSTAT_CHANNELS_CONFIG[i].relayPin]);
  }

  bool stateChanged = nextState != shiftRegisterState;
  if (stateChanged) {
    shiftRegisterState = nextState;
  }

  if (stateChanged || (nowMs - lastShiftRegisterRefreshMs) >= SHIFT_REGISTER_REFRESH_PERIOD_MS) {
    applyShiftRegisterState();
  }
}

// Функция для загрузки настроек расписания из JSON
void loadScheduleFromJson(JsonArray& array, std::vector<ThermostatSchedule>& schedule) {
  schedule.clear();
  for (size_t i = 0; i < array.size(); i++) {
    ThermostatSchedule item;
    item.hour = array[i]["hour"].as<int>();
    item.minute = array[i]["minute"].as<int>();
    item.temperature = array[i]["temperature"].as<float>();
    item.dayOfWeek = array[i]["dayOfWeek"].as<int>();
    schedule.push_back(item);
  }
}

// Функция для сохранения расписания в JSON
void saveScheduleToJson(JsonArray& array, const std::vector<ThermostatSchedule>& schedule) {
  for (size_t i = 0; i < schedule.size(); i++) {
    JsonObject item = array.createNestedObject();
    item["hour"] = schedule[i].hour;
    item["minute"] = schedule[i].minute;
    item["temperature"] = schedule[i].temperature;
    item["dayOfWeek"] = schedule[i].dayOfWeek;
  }
}

// Функция для создания настроек ThermostatSettings из конфигурации канала
ThermostatSettings createThermostatSettingsFromConfig(const ThermostatChannelConfig& config, bool enabled = true) {
  return ThermostatSettings(
    config.defaultSchedule,
    config.defaultScheduleSize,
    enabled
  );
}

void loadThermostatSettingsFromConfig(){
 for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
    thermostatSettings[i] = createThermostatSettingsFromConfig(THERMOSTAT_CHANNELS_CONFIG[i]);
  }
} 

// Загрузка настроек теплого пола из файла
void loadThermostatSettingsFromFile() {
  // Инициализация настроек дефолтными значениями
  loadThermostatSettingsFromConfig();
  
  if (LittleFS.exists("/settings.json")) {
    File settingsFile = LittleFS.open("/settings.json", "r");
    if (settingsFile) {
      // Serial.println("Loading thermostat settings from file");
      
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, settingsFile);
      
      if (!error) {
        // Проверяем наличие массива каналов
        if (doc.containsKey("channels") && doc["channels"].is<JsonArray>()) {
          JsonArray channels = doc["channels"].as<JsonArray>();
          
          // Загружаем настройки из массива
          for (int i = 0; i < min(THERMOSTAT_CHANNELS_NUM, (int)channels.size()); i++) {
            JsonObject channelObj = channels[i].as<JsonObject>();
            
            // Проверяем, что это правильный канал, сравнивая имя
            if (channelObj.containsKey("name") && String(channelObj["name"].as<const char*>()) == String(THERMOSTAT_CHANNELS_CONFIG[i].name)) {
              if (channelObj.containsKey("schedule") && channelObj["schedule"].is<JsonArray>()) {
                JsonArray scheduleArray = channelObj["schedule"].as<JsonArray>();
                std::vector<ThermostatSchedule> schedule;
                loadScheduleFromJson(scheduleArray, schedule);
                thermostatSettings[i].schedule = schedule;
              }
              
              if (channelObj.containsKey("enabled")) {
                thermostatSettings[i].enabled = channelObj["enabled"].as<bool>();
              }
            }
          }
        }
        
        // Serial.println("Thermostat settings loaded successfully");
      } else {
        // Serial.println("Failed to parse settings file");
      }
      
      settingsFile.close();
    }
  } else {
    // Создаем файл с дефолтными настройками
    saveThermostatSettingsFromFile();
  }
}

// Сохранение настроек теплого пола в файл
void saveThermostatSettingsFromFile() {
  DynamicJsonDocument doc(2048);
  
  // Создаем массив каналов
  JsonArray channels = doc.createNestedArray("channels");
  
  // Получаем актуальные настройки и сохраняем в массив
  for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
    if (thermostats[i] != nullptr) {
      ThermostatSettings settings = thermostats[i]->getSettings();
      
      // Создаем объект для текущего канала
      JsonObject channelObj = channels.createNestedObject();
      
      // Сохраняем идентификатор канала
      channelObj["name"] = THERMOSTAT_CHANNELS_CONFIG[i].name;
      channelObj["displayName"] = THERMOSTAT_CHANNELS_CONFIG[i].displayName;
      channelObj["enabled"] = settings.enabled;
      
      // Создаем массив расписания
      JsonArray scheduleArray = channelObj.createNestedArray("schedule");
      saveScheduleToJson(scheduleArray, settings.schedule);
    }
  }
  
  File settingsFile = LittleFS.open("/settings.json", "w");
  if (settingsFile) {
    serializeJson(doc, settingsFile);
    settingsFile.close();
    // Serial.println("Thermostat settings saved to file");
  } else {
    // Serial.println("Failed to create settings file");
  }
}

void setup() {
  // Serial.begin(115200);
  // Serial.println("Booting");

  initializeConfig();

  pinMode(SHIFT_REGISTER_DATA_PIN, OUTPUT);
  pinMode(SHIFT_REGISTER_LATCH_PIN, OUTPUT);
  pinMode(SHIFT_REGISTER_CLOCK_PIN, OUTPUT);
  digitalWrite(SHIFT_REGISTER_DATA_PIN, LOW);
  digitalWrite(SHIFT_REGISTER_CLOCK_PIN, LOW);
  digitalWrite(SHIFT_REGISTER_LATCH_PIN, HIGH);
  applyShiftRegisterState();

  // Инициализируем выходы для реле
  for (int i=0; i<RELAY_NUM; i++){
    relay_states[i] = false;
    pinMode(RELAY_PIN[i], OUTPUT);
    apply_relay_state(i);
  }
  updateStatusLeds(millis());

  // Инициализируем файловую систему LittleFS
  littleFsAvailable = LittleFS.begin();
  if (!littleFsAvailable) {
    // Serial.println("Failed to mount LittleFS");
    
    // Даже если не получилось загрузить LittleFS, инициализируем с дефолтными настройками
    loadThermostatSettingsFromConfig();
  } else {
    // Serial.println("LittleFS mounted successfully");
    // Загружаем настройки из файла
    loadThermostatSettingsFromFile();
  }

  loadTimeSettingsFromFile();
  
  // Инициализируем контроллеры теплого пола с загруженными настройками
  for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
    const uint8_t sensorIndex = THERMOSTAT_CHANNELS_CONFIG[i].temperatureSensor;
    const uint8_t relayIndex = THERMOSTAT_CHANNELS_CONFIG[i].relayPin;
    
    thermostats[i] = new Thermostat(
      thermostatSettings[i],
      [sensorIndex]() -> float {
        if (DS18B20_values[sensorIndex].hasActualValue()) {
          return DS18B20_values[sensorIndex].temperature;
        }
        return DEVICE_DISCONNECTED_C;
      },
      [relayIndex](bool state) {
        relay_state(relayIndex, state);
      }
    );

    thermostats[i]->setStateChangedCallback([i](const ThermostatState& state) {
      publishMqttThermostatState(i, state);
    });
  }
  
  // Инициализируем объекты управления вентиляторами с лямбда-функциями
  for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
    const uint8_t relayIndex = RELAY_CHANNELS_CONFIG[i].relayPin;
    
    relays[i] = new Relay([relayIndex](bool state) {
      relay_state(relayIndex, state);
    }, RELAY_CHANNELS_CONFIG[i].defaultTimerMinutes);

    relays[i]->setStateChangedCallback([i](const RelayState& state) {
      publishMqttRelayState(i, state);
    });
  }

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(MQTT_CLIENT_ID);
  updateStatusLeds(millis());

  ArduinoOTA.setHostname("relay-controller");
  
  ArduinoOTA.onStart([]() {
    // Serial.println("ArduinoOTA start update");
  });

  ArduinoOTA.begin();

  setupMqttClient();

  if (!applyTimeZoneById(currentTimeZoneId)) {
    applyTimeZoneById(getDefaultTimeZone()->id);
  }

  updateWiFiState(millis());

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });


  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    scheduleReboot();
    request->send(200, "text/plain", "Reboot scheduled\n");
  });


  server.on("/api/temperatures", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    JsonArray sensors = doc.createNestedArray("sensors");
    
    for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
      JsonObject sensorObj = sensors.createNestedObject();
      sensorObj["id"] = formatDeviceId(DS18B20_DEVICES[i]);
      sensorObj["location"] = DS18B20_DEVICES_LOCATIONS[i];
      if (DS18B20_values[i].hasValue()) {
        sensorObj["temperature"] = DS18B20_values[i].temperature;
        sensorObj["timestamp"] = static_cast<long>(DS18B20_values[i].timestamp);
      } else {
        sensorObj["temperature"] = nullptr;
        sensorObj["timestamp"] = nullptr;
      }
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  setupConfigApiRoutes(server);

  // API для управления системой теплого пола
  AsyncCallbackWebHandler* thermostatControlHandler = new AsyncCallbackWebHandler();
  thermostatControlHandler->setUri("/api/thermostat/control");
  thermostatControlHandler->setMethod(HTTP_POST);
  thermostatControlHandler->onRequest([](AsyncWebServerRequest *request) {});
  
  thermostatControlHandler->onBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (total > 0 && index == 0) {
      // Выделяем память под весь буфер
      request->_tempObject = malloc(total + 1);
      if (request->_tempObject == NULL) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Недостаточно памяти\"}");
        return;
      }
    }

    // Копируем данные в буфер
    if (request->_tempObject) {
      memcpy((uint8_t*)request->_tempObject + index, data, len);
      
      // Если получены все данные, обрабатываем JSON
      if (index + len == total) {
        ((uint8_t*)request->_tempObject)[total] = '\0'; // Добавляем нулевой символ
        String jsonStr = String((char*)request->_tempObject);
        
        DynamicJsonDocument doc(128);
        DeserializationError error = deserializeJson(doc, jsonStr);
        
        bool success = false;
        String message = "";
        
        if (!error) {
          // Проверяем наличие параметров канала и состояния
          if (doc.containsKey("channel") && doc.containsKey("state")) {
            String channelName = doc["channel"].as<String>();
            String state = doc["state"].as<String>();
            bool enable = (state == "on");
            
            // Ищем канал по имени
            bool found = false;
            for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
              if (channelName == THERMOSTAT_CHANNELS_CONFIG[i].name) {
                thermostats[i]->setOn(enable);
                saveThermostatSettingsFromFile(); // Сохраняем новое состояние в файл
                message = String(THERMOSTAT_CHANNELS_CONFIG[i].displayName) + (enable ? " включен" : " выключен");
                success = true;
                found = true;
                break;
              }
            }
            
            if (!found) {
              message = "Неизвестный канал теплого пола: " + channelName;
            }
          } else {
            message = "Отсутствуют необходимые параметры channel и state";
          }
        } else {
          message = "Ошибка разбора JSON";
        }
        
        // Формируем ответ
        DynamicJsonDocument response(256);
        response["success"] = success;
        response["message"] = message;
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(success ? 200 : 400, "application/json", responseStr);
        
        // Освобождаем память
        free(request->_tempObject);
        request->_tempObject = NULL;
      }
    }
  });
  
  server.addHandler(thermostatControlHandler);
  
  // Эндпоинт для получения состояния вентиляторов
  server.on("/api/relay/state", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);

    // Формируем JSON с данными всех каналов вентиляции
    for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
      const char* channelName = RELAY_CHANNELS_CONFIG[i].name;
      JsonObject channelObj = doc.createNestedObject(channelName);

      channelObj["displayName"] = RELAY_CHANNELS_CONFIG[i].displayName;

      if (relays[i] == nullptr) {
        channelObj["on"] = false;
        channelObj["relayState"] = false;
        channelObj["remainingTime"] = 0;
        continue;
      }

      RelayState state = relays[i]->getState();
      channelObj["on"] = state.on;
      channelObj["relayState"] = state.relayState;
      channelObj["remainingTime"] = state.remainingTime;
    }

    String responseStr;
    serializeJson(doc, responseStr);
    request->send(200, "application/json", responseStr);
  });
  
  // Обработчик для POST запросов к вентиляторам
  AsyncCallbackWebHandler* newRelayHandler = new AsyncCallbackWebHandler();
  newRelayHandler->setUri("/api/relay/control");
  newRelayHandler->setMethod(HTTP_POST);
  newRelayHandler->onRequest([](AsyncWebServerRequest *request) {});
  
  newRelayHandler->onBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (total > 0 && index == 0) {
      // Выделяем память под весь буфер
      request->_tempObject = malloc(total + 1);
      if (request->_tempObject == NULL) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Недостаточно памяти\"}");
        return;
      }
    }

    // Копируем данные в буфер
    if (request->_tempObject) {
      memcpy((uint8_t*)request->_tempObject + index, data, len);
      
      // Если получены все данные, обрабатываем JSON
      if (index + len == total) {
        ((uint8_t*)request->_tempObject)[total] = '\0'; // Добавляем нулевой символ
        String jsonStr = String((char*)request->_tempObject);
        
        DynamicJsonDocument doc(128);
        DeserializationError error = deserializeJson(doc, jsonStr);
        
        bool success = false;
        String message = "";
        
        if (!error) {
          // Проверяем наличие идентификатора канала вентилятора
          int relayIndex = RELAY_TOILET_CHANNEL; // По умолчанию - вентилятор туалета
          Relay* targetRelay = nullptr;
          
          if (doc.containsKey("channel")) {
            String relayId = doc["channel"].as<String>();
            
            // Ищем канал вентилятора по имени
            bool found = false;
            for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
              if (relayId == RELAY_CHANNELS_CONFIG[i].name) {
                relayIndex = i;
                found = true;
                break;
              }
            }
            
            if (!found) {
              message = "Неизвестный канал вентилятора: " + relayId;
              success = false;
              
              // Отправляем ответ об ошибке и освобождаем память
              DynamicJsonDocument response(128);
              response["success"] = success;
              response["message"] = message;
              
              String responseStr;
              serializeJson(response, responseStr);
              request->send(400, "application/json", responseStr);
              free(request->_tempObject);
              request->_tempObject = NULL;
              return;
            }
          }
          
          targetRelay = relays[relayIndex];
          String relayName = RELAY_CHANNELS_CONFIG[relayIndex].displayName;
          
          if (doc.containsKey("state")) {
            String stateArg = doc["state"].as<String>();
            if (stateArg == "on") {
              targetRelay->turnOn();
              RelayState state = targetRelay->getState();
              if (!state.on) {
                message = relayName + " глобально отключен";
              } else {
                message = relayName + " включен";
              }
              success = true;
            } else if (stateArg == "off") {
              targetRelay->turnOff();
              message = relayName + " выключен";
              success = true;
            } else if (stateArg == "toggle") {
              targetRelay->toggle();
              RelayState state = targetRelay->getState();
              if (!state.on) {
                message = relayName + " глобально отключен";
                success = true;
              } else {
                String newState = state.relayState ? "включен" : "выключен";
                message = relayName + " " + newState;
                success = true;
              }
            } else {
              message = "Неверный параметр состояния";
            }
          } else if (doc.containsKey("timer")) {
            int minutes = doc["timer"].as<int>();
            if (minutes > 0 && minutes <= 180) { // maximum 3 hours
              targetRelay->turnOnWithTimer(minutes);
              RelayState state = targetRelay->getState();
              if (!state.on) {
                message = relayName + " глобально отключен";
              } else {
                message = relayName + " включен на " + String(minutes) + " минут";
              }
              success = true;
            } else {
              message = "Неверное значение таймера";
            }
          } else {
            message = "Отсутствуют необходимые параметры";
          }
        } else {
          message = "Ошибка разбора JSON";
        }
        
        // Формируем ответ
        DynamicJsonDocument response(128);
        response["success"] = success;
        response["message"] = message;
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(success ? 200 : 400, "application/json", responseStr);
        
        // Освобождаем память
        free(request->_tempObject);
        request->_tempObject = NULL;
      }
    }
  });
  
  server.addHandler(newRelayHandler);
  
  // API для получения текущих настроек теплого пола
  server.on("/api/thermostat/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    
    // Создаем массив каналов
    JsonArray channels = doc.createNestedArray("channels");
    
    // Добавляем информацию о каждом канале
    for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
      JsonObject channelObj = channels.createNestedObject();
      
      // Добавляем идентификатор и отображаемое имя канала
      channelObj["name"] = THERMOSTAT_CHANNELS_CONFIG[i].name;
      channelObj["displayName"] = THERMOSTAT_CHANNELS_CONFIG[i].displayName;
      channelObj["enabled"] = thermostatSettings[i].enabled;
      
      // Добавляем расписание
      JsonArray scheduleArray = channelObj.createNestedArray("schedule");
      saveScheduleToJson(scheduleArray, thermostatSettings[i].schedule);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для получения текущего состояния отопления
  server.on("/api/thermostat/state", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    
    ThermostatState _state;
    
    // Формируем JSON с данными всех каналов
    for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
      JsonObject channelObj = doc.createNestedObject(THERMOSTAT_CHANNELS_CONFIG[i].name);
      thermostats[i]->getState(&_state);
      
      channelObj["on"] = _state.on;
      if (_state.on) {
        channelObj["relayState"] = _state.relayState;
        channelObj["currentTemperature"] = _state.currentTemperature;
        channelObj["desiredTemperature"] = _state.desiredTemperature;
      }
      channelObj["displayName"] = THERMOSTAT_CHANNELS_CONFIG[i].displayName;
    }
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    request->send(200, "application/json", jsonOutput);
  });
  
  // API для обновления настроек теплого пола
  AsyncCallbackWebHandler* newUpdateSettingsHandler = new AsyncCallbackWebHandler();
  newUpdateSettingsHandler->onRequest([](AsyncWebServerRequest *request) {
    // Обрабатываем только POST запросы
    if (request->method() != HTTP_POST) {
      request->send(405, "text/plain", "Method Not Allowed");
      return;
    }
    // Будем выполнять обработку в onBody
  });

  newUpdateSettingsHandler->onBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (total > 0 && index == 0) {
      // Выделяем память под весь буфер
      request->_tempObject = malloc(total + 1);
      if (request->_tempObject == NULL) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Not enough memory\"}");
        return;
      }
    }

    // Копируем данные в буфер
    if (request->_tempObject) {
      memcpy((uint8_t*)request->_tempObject + index, data, len);
      
      // Если получены все данные, обрабатываем JSON
      if (index + len == total) {
        ((uint8_t*)request->_tempObject)[total] = '\0'; // Добавляем нулевой символ
        String jsonStr = String((char*)request->_tempObject);
        
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, jsonStr);
        
        bool success = false;
        String message = "Settings updated successfully";
        
        if (!error) {
          // Обработка JSON с массивом channels
          if (doc.containsKey("channels") && doc["channels"].is<JsonArray>()) {
            JsonArray channels = doc["channels"].as<JsonArray>();
            
            // Обновляем настройки для каждого канала из массива
            for (JsonObject channelObj : channels) {
              if (channelObj.containsKey("name")) {
                String channelName = channelObj["name"].as<String>();
                
                // Ищем индекс канала по имени
                for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
                  if (channelName == THERMOSTAT_CHANNELS_CONFIG[i].name) {
                    // Получаем текущие настройки канала
                    ThermostatSettings tempSettings = thermostats[i]->getSettings();
                    
                    // Обновляем состояние включения
                    if (channelObj.containsKey("enabled")) {
                      tempSettings.enabled = channelObj["enabled"].as<bool>();
                    }
                    
                    // Обновляем расписание
                    if (channelObj.containsKey("schedule") && channelObj["schedule"].is<JsonArray>()) {
                      JsonArray scheduleArray = channelObj["schedule"].as<JsonArray>();
                      std::vector<ThermostatSchedule> schedule;
                      loadScheduleFromJson(scheduleArray, schedule);
                      tempSettings.schedule = schedule;
                    }
                    
                    // Применяем настройки
                    thermostats[i]->applySettings(tempSettings);
                    
                    // Обновляем глобальную настройку
                    thermostatSettings[i] = tempSettings;
                    
                    success = true;
                    break;
                  }
                }
              }
            }
          } else {
            message = "Invalid JSON format: 'channels' array is required";
            success = false;
          }
          
          // Сохраняем настройки в LittleFS
          if (success) {
            saveThermostatSettingsFromFile();
          }
        } else {
          message = "Failed to parse JSON";
          success = false;
        }
        
        // Формируем ответ
        DynamicJsonDocument response(256);
        response["success"] = success;
        response["message"] = message;
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        
        // Освобождаем память
        free(request->_tempObject);
        request->_tempObject = NULL;
      }
    }
  });
  
  newUpdateSettingsHandler->setUri("/api/thermostat/settings");
  server.addHandler(newUpdateSettingsHandler);
  
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/config.html", "text/html");
  });

  // Обработчик для корневого маршрута - отдаем index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (wifiState == WIFI_STATE_AP_MODE) {
      request->redirect("/config");
      return;
    }
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  // Обработчик для загрузки статических файлов (CSS, JS, изображения)
  server.serveStatic("/", LittleFS, "/");
  
  server.onNotFound( [](AsyncWebServerRequest *request) {
    if (wifiState == WIFI_STATE_AP_MODE) {
      request->redirect("/config");
      return;
    }
    server_response(request, 404);
  });

  server.begin();

  pinMode(ONE_WIRE_SUPPLY_PIN, OUTPUT);
  applyOneWireEnable(true);

  for (int i=0; i<ONE_WIRE_NUM_DEVICES; i++){
    DS18B20_values[i].temperature = 0;
    DS18B20_values[i].timestamp = 0;
    DS18B20_values[i].lastReadMs = 0;
    DS18B20_values[i].hasRecentReading = false;
  }
  DS18B20.begin();
  DS18B20.setResolution(10);
  oneWireWatchdog.start(millis());

  // Настройка обработчика кнопки для управления вентилятором туалета
  inputs.on(BUTTON_PIN, STATE_LOW, BUTTON_TIMEOUT, [](uint8_t state){
      (void)state;
      relays[RELAY_TOILET_CHANNEL]->toggle(); // Переключить состояние вентилятора туалета
      publishMqttButtonEvent("PRESS");
  }); 
}

bool isValidT(float t){
  return t > -55 && t < 85;
}

bool isOneWireAlive() {
  for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
    if (DS18B20_values[i].hasActualValue()) {
      return true;
    }
  }
  return false;
}

void server_response(AsyncWebServerRequest *request, unsigned int response) {
  switch (response) {
    case 200:
      request->send(200);
      break;
    case 400:
      request->send(400, "text/plain", "Bad request\n\n");
      break;
    default:
      //case 404:
      request->send(404, "text/plain", "File Not Found\n\n");
      break;
  }
}

int prev_n = -1;

void loop() {
  unsigned long m = millis();
  updateWiFiState(m);

  if (
    wifiState == WIFI_STATE_STA_CONNECTED &&
    WiFi.status() == WL_CONNECTED &&
    !mqttClient.connected() &&
    hasConfiguredMqttSettings() &&
    (m - lastMqttReconnect) >= MQTT_RECONNECT_PERIOD_MS
  ) {
    lastMqttReconnect = m;
    connectMqtt();
  }

  if (mqttClient.connected()) {
    if (!mqttWasConnected) {
      mqttWasConnected = true;
      publishMqttStartMessages();
    }
  } else {
    mqttWasConnected = false;
  }

  if (oneWirePowerEnabled) {
    if (ds18b20NeedsRequest) {
      DS18B20.requestTemperatures();
      ds18b20NeedsRequest = false;
    }

    int p = m % DS18B20_REQUEST_PERIOD;
    if (p == 0){
      int n = (m / DS18B20_REQUEST_PERIOD) % DS18B20_NUM_REQUESTS;
      if (n != prev_n){
        prev_n = n;
        if (n == 0){
          DS18B20.requestTemperatures();
        }else{
          float temperature = DS18B20.getTempC(DS18B20_DEVICES[n-1]);
          if (isValidT(temperature)){
            DS18B20_values[n-1].temperature = temperature;
            time_t ts = time(nullptr);
            DS18B20_values[n-1].timestamp = ts;
            DS18B20_values[n-1].lastReadMs = m;
            DS18B20_values[n-1].hasRecentReading = true;
            publishMqttSensorState(n-1);
          }
        }
      }
    }
  }

  oneWireWatchdog.handle(m, isOneWireAlive());

  // Обработка всех каналов теплого пола
  for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
    if (thermostats[i] != nullptr) {
      thermostats[i]->handle();
    }
  }

  // Обработка таймеров всех вентиляторов
  for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
    if (relays[i] != nullptr) {
      relays[i]->handle();
    }
  }

  inputs.handle();
  updateStatusLeds(m);
  
  if (rebootScheduledAt != 0 && static_cast<long>(m - rebootScheduledAt) >= 0) {
    ESP.restart();
  }

  ArduinoOTA.handle();
  yield();
}
