/**
    Use 3.1.2 esp8266 core
    lwip v2 Higher bandwidth; CPU 80 MHz
    4M (FS: 1Mb OTA:~1019Kb)

    dependencies:
    https://github.com/me-no-dev/ESPAsyncWebServer
    https://github.com/ar2rus/ClunetMulticast

 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include "RelayController.h"
#include "Credentials.h"

#include "heatfloor.h"
#include "FanController.h"

#include <ESPInputs.h>

#include <ArduinoJson.h>

#include <time.h>
#include <LittleFS.h>  // Используем LittleFS

#include <vector>

Inputs inputs;

// Массивы для настроек и контроллеров теплого пола
FloorHeatingSettings heatingSettings[HEATING_CHANNELS_NUM];
FloorHeatingController* heatingControllers[HEATING_CHANNELS_NUM];

// Массив контроллеров вентиляторов
FanController* fanControllers[FAN_CHANNELS_NUM];

bool oneWireEnabled = true;

void oneWireEnable(bool enabled){
  if (oneWireEnabled != enabled){
    oneWireEnabled = enabled;
    applyOneWireEnable();
  }
}

void applyOneWireEnable(){
  digitalWrite(ONE_WIRE_SUPPLY_PIN, oneWireEnabled ? HIGH : LOW);
}

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature DS18B20(&oneWire);

float DS18B20_values[ONE_WIRE_NUM_DEVICES];

int DS18B20_errors[ONE_WIRE_NUM_DEVICES];
long DS18B20_last[ONE_WIRE_NUM_DEVICES];
long DS18B20_unavailables[ONE_WIRE_NUM_DEVICES];

bool relay_states[RELAY_NUM];

void relay_state(int index, bool _on){
  if (relay_states[index] != _on){
     relay_states[index] = _on;
     apply_relay_state(index);
  }
}

void apply_relay_state(int index){
  digitalWrite(RELAY_PIN[index], relay_states[index] ? HIGH : LOW);
}

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 50, 129); //Node static IP
IPAddress gateway(192, 168, 50, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsAddr(192, 168, 50, 1);

AsyncWebServer server(80); // Порт 80
ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);
bool littleFsAvailable = false;

static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.nist.gov";
static const char* TIME_SETTINGS_FILE = "/time.json";
static const long MIN_VALID_EPOCH = 1609459200; // 2021-01-01

static const TimeZoneOption TIME_ZONES[] = {
  { "UTC", "UTC", TZKEY_UTC },
  { "Europe/Kaliningrad", "Europe/Kaliningrad", TZKEY_Europe_Kaliningrad },
  { "Europe/Moscow", "Europe/Moscow", TZKEY_Europe_Moscow },
  { "Europe/Samara", "Europe/Samara", TZKEY_Europe_Samara },
  { "Asia/Yekaterinburg", "Asia/Yekaterinburg", TZKEY_Asia_Yekaterinburg },
  { "Asia/Omsk", "Asia/Omsk", TZKEY_Asia_Omsk },
  { "Asia/Krasnoyarsk", "Asia/Krasnoyarsk", TZKEY_Asia_Krasnoyarsk },
  { "Asia/Irkutsk", "Asia/Irkutsk", TZKEY_Asia_Irkutsk },
  { "Asia/Yakutsk", "Asia/Yakutsk", TZKEY_Asia_Yakutsk },
  { "Asia/Vladivostok", "Asia/Vladivostok", TZKEY_Asia_Vladivostok },
  { "Asia/Magadan", "Asia/Magadan", TZKEY_Asia_Magadan },
  { "Asia/Kamchatka", "Asia/Kamchatka", TZKEY_Asia_Kamchatka },
  { "America/New_York", "America/New_York", TZKEY_America_New_York },
  { "America/Chicago", "America/Chicago", TZKEY_America_Chicago },
  { "America/Denver", "America/Denver", TZKEY_America_Denver },
  { "America/Los_Angeles", "America/Los_Angeles", TZKEY_America_Los_Angeles }
};

static const size_t TIME_ZONES_COUNT = sizeof(TIME_ZONES) / sizeof(TIME_ZONES[0]);

String currentTimeZoneId = DEFAULT_TIMEZONE_ID;

String formatDeviceId(const uint8_t* address) {
  char buffer[17];
  snprintf(
    buffer,
    sizeof(buffer),
    "%02X%02X%02X%02X%02X%02X%02X%02X",
    address[0], address[1], address[2], address[3],
    address[4], address[5], address[6], address[7]
  );
  return String(buffer);
}

const char* getPosixTimeZone(TimeZoneKey key) {
  switch (key) {
    case TZKEY_Europe_Kaliningrad:
      return TZ_Europe_Kaliningrad;
    case TZKEY_Europe_Moscow:
      return TZ_Europe_Moscow;
    case TZKEY_Europe_Samara:
      return TZ_Europe_Samara;
    case TZKEY_Asia_Yekaterinburg:
      return TZ_Asia_Yekaterinburg;
    case TZKEY_Asia_Omsk:
      return TZ_Asia_Omsk;
    case TZKEY_Asia_Krasnoyarsk:
      return TZ_Asia_Krasnoyarsk;
    case TZKEY_Asia_Irkutsk:
      return TZ_Asia_Irkutsk;
    case TZKEY_Asia_Yakutsk:
      return TZ_Asia_Yakutsk;
    case TZKEY_Asia_Vladivostok:
      return TZ_Asia_Vladivostok;
    case TZKEY_Asia_Magadan:
      return TZ_Asia_Magadan;
    case TZKEY_Asia_Kamchatka:
      return TZ_Asia_Kamchatka;
    case TZKEY_America_New_York:
      return TZ_America_New_York;
    case TZKEY_America_Chicago:
      return TZ_America_Chicago;
    case TZKEY_America_Denver:
      return TZ_America_Denver;
    case TZKEY_America_Los_Angeles:
      return TZ_America_Los_Angeles;
    case TZKEY_UTC:
    default:
      return "UTC0";
  }
}

// Функция для загрузки настроек расписания из JSON
void loadScheduleFromJson(JsonArray& array, std::vector<FloorHeatingSchedule>& schedule) {
  schedule.clear();
  for (size_t i = 0; i < array.size(); i++) {
    FloorHeatingSchedule item;
    item.hour = array[i]["hour"].as<int>();
    item.minute = array[i]["minute"].as<int>();
    item.temperature = array[i]["temperature"].as<float>();
    item.dayOfWeek = array[i]["dayOfWeek"].as<int>();
    schedule.push_back(item);
  }
}

// Функция для сохранения расписания в JSON
void saveScheduleToJson(JsonArray& array, const std::vector<FloorHeatingSchedule>& schedule) {
  for (size_t i = 0; i < schedule.size(); i++) {
    JsonObject item = array.createNestedObject();
    item["hour"] = schedule[i].hour;
    item["minute"] = schedule[i].minute;
    item["temperature"] = schedule[i].temperature;
    item["dayOfWeek"] = schedule[i].dayOfWeek;
  }
}

const TimeZoneOption* findTimeZoneById(const String& id) {
  for (size_t i = 0; i < TIME_ZONES_COUNT; i++) {
    if (id == TIME_ZONES[i].id) {
      return &TIME_ZONES[i];
    }
  }
  return nullptr;
}

const TimeZoneOption* getDefaultTimeZone() {
  const TimeZoneOption* zone = findTimeZoneById(DEFAULT_TIMEZONE_ID);
  if (zone != nullptr) {
    return zone;
  }
  return &TIME_ZONES[0];
}

bool applyTimeZoneById(const String& id) {
  const TimeZoneOption* zone = findTimeZoneById(id);
  if (zone == nullptr) {
    return false;
  }
  currentTimeZoneId = zone->id;
  configTime(getPosixTimeZone(zone->key), NTP_SERVER_1, NTP_SERVER_2);
  return true;
}

void loadTimeSettingsFromFile() {
  currentTimeZoneId = DEFAULT_TIMEZONE_ID;
  if (!littleFsAvailable) {
    return;
  }

  if (!LittleFS.exists(TIME_SETTINGS_FILE)) {
    return;
  }

  File settingsFile = LittleFS.open(TIME_SETTINGS_FILE, "r");
  if (!settingsFile) {
    return;
  }

  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, settingsFile);
  settingsFile.close();

  if (error) {
    return;
  }

  if (doc.containsKey("timezone")) {
    String tzId = doc["timezone"].as<String>();
    if (findTimeZoneById(tzId) != nullptr) {
      currentTimeZoneId = tzId;
    }
  }
}

void saveTimeSettingsToFile() {
  if (!littleFsAvailable) {
    return;
  }

  DynamicJsonDocument doc(256);
  doc["timezone"] = currentTimeZoneId;

  File settingsFile = LittleFS.open(TIME_SETTINGS_FILE, "w");
  if (!settingsFile) {
    return;
  }

  serializeJson(doc, settingsFile);
  settingsFile.close();
}

void appendTimePayload(DynamicJsonDocument& doc) {
  time_t now = time(nullptr);
  bool valid = now > MIN_VALID_EPOCH;

  doc["epoch"] = static_cast<long>(now);
  doc["timeZone"] = currentTimeZoneId;
  doc["valid"] = valid;

  if (valid) {
    struct tm timeInfo;
    localtime_r(&now, &timeInfo);
    char timeBuffer[16];
    strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeInfo);
    doc["time"] = timeBuffer;
  } else {
    doc["time"] = "";
  }
}

// Функция для создания настроек FloorHeatingSettings из конфигурации канала
FloorHeatingSettings createSettingsFromConfig(const HeatingChannelConfig& config, bool enabled = true) {
  return FloorHeatingSettings(
    config.defaultSchedule,
    config.defaultScheduleSize,
    enabled
  );
}

void loadSettingFromConfig(){
 for (int i = 0; i < HEATING_CHANNELS_NUM; i++) {
    heatingSettings[i] = createSettingsFromConfig(HEATING_CHANNELS_CONFIG[i]);
  }
} 

// Загрузка настроек теплого пола из файла
void loadHeatfloorSettingsFromFile() {
  // Инициализация настроек дефолтными значениями
  loadSettingFromConfig();
  
  if (LittleFS.exists("/settings.json")) {
    File settingsFile = LittleFS.open("/settings.json", "r");
    if (settingsFile) {
      Serial.println("Loading heatfloor settings from file");
      
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, settingsFile);
      
      if (!error) {
        // Проверяем наличие массива каналов
        if (doc.containsKey("channels") && doc["channels"].is<JsonArray>()) {
          JsonArray channels = doc["channels"].as<JsonArray>();
          
          // Загружаем настройки из массива
          for (int i = 0; i < min(HEATING_CHANNELS_NUM, (int)channels.size()); i++) {
            JsonObject channelObj = channels[i].as<JsonObject>();
            
            // Проверяем, что это правильный канал, сравнивая имя
            if (channelObj.containsKey("name") && String(channelObj["name"].as<const char*>()) == String(HEATING_CHANNELS_CONFIG[i].name)) {
              if (channelObj.containsKey("schedule") && channelObj["schedule"].is<JsonArray>()) {
                JsonArray scheduleArray = channelObj["schedule"].as<JsonArray>();
                std::vector<FloorHeatingSchedule> schedule;
                loadScheduleFromJson(scheduleArray, schedule);
                heatingSettings[i].schedule = schedule;
              }
              
              if (channelObj.containsKey("enabled")) {
                heatingSettings[i].enabled = channelObj["enabled"].as<bool>();
              }
            }
          }
        }
        
        Serial.println("Heatfloor settings loaded successfully");
      } else {
        Serial.println("Failed to parse settings file");
      }
      
      settingsFile.close();
    }
  } else {
    // Создаем файл с дефолтными настройками
    saveHeatfloorSettingsToFile();
  }
}

// Сохранение настроек теплого пола в файл
void saveHeatfloorSettingsToFile() {
  DynamicJsonDocument doc(2048);
  
  // Создаем массив каналов
  JsonArray channels = doc.createNestedArray("channels");
  
  // Получаем актуальные настройки и сохраняем в массив
  for (int i = 0; i < HEATING_CHANNELS_NUM; i++) {
    if (heatingControllers[i] != nullptr) {
      FloorHeatingSettings settings = heatingControllers[i]->getSettings();
      
      // Создаем объект для текущего канала
      JsonObject channelObj = channels.createNestedObject();
      
      // Сохраняем идентификатор канала
      channelObj["name"] = HEATING_CHANNELS_CONFIG[i].name;
      channelObj["displayName"] = HEATING_CHANNELS_CONFIG[i].displayName;
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
    Serial.println("Heatfloor settings saved to file");
  } else {
    Serial.println("Failed to create settings file");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  // Инициализируем выходы для реле
  for (int i=0; i<RELAY_NUM; i++){
    pinMode(RELAY_PIN[i], OUTPUT);
    apply_relay_state(i);
  }

  // Инициализируем файловую систему LittleFS
  littleFsAvailable = LittleFS.begin();
  if (!littleFsAvailable) {
    Serial.println("Failed to mount LittleFS");
    
    // Даже если не получилось загрузить LittleFS, инициализируем с дефолтными настройками
    loadSettingFromConfig();
  } else {
    Serial.println("LittleFS mounted successfully");
    // Загружаем настройки из файла
    loadHeatfloorSettingsFromFile();
  }

  loadTimeSettingsFromFile();
  
  // Инициализируем контроллеры теплого пола с загруженными настройками
  for (int i = 0; i < HEATING_CHANNELS_NUM; i++) {
    const uint8_t sensorIndex = HEATING_CHANNELS_CONFIG[i].temperatureSensor;
    const uint8_t relayIndex = HEATING_CHANNELS_CONFIG[i].relayPin;
    
    heatingControllers[i] = new FloorHeatingController(
      heatingSettings[i],
      [sensorIndex]() -> float {
        return DS18B20_values[sensorIndex];
      },
      [relayIndex](bool state) {
        relay_state(relayIndex, state);
      }
    );
  }
  
  // Инициализируем объекты управления вентиляторами с лямбда-функциями
  for (int i = 0; i < FAN_CHANNELS_NUM; i++) {
    const uint8_t relayIndex = FAN_CHANNELS_CONFIG[i].relayPin;
    
    fanControllers[i] = new FanController([relayIndex](bool state) {
      relay_state(relayIndex, state);
    }, FAN_CHANNELS_CONFIG[i].defaultTimerMinutes);
  }

  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet, dnsAddr);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(1000);
    ESP.restart();
  }

  ArduinoOTA.setHostname("relay-controller");
  
  ArduinoOTA.onStart([]() {
    Serial.println("ArduinoOTA start update");
  });

  ArduinoOTA.begin();

  if (!applyTimeZoneById(currentTimeZoneId)) {
    applyTimeZoneById(getDefaultTimeZone()->id);
  }
  
  if (clunet.connect()){
    clunet.onPacketReceived([](clunet_packet* packet){
       switch (packet->command) {
        case CLUNET_COMMAND_FAN: {
          if (packet->size == 0) {
            fanControllers[FAN_BATHROOM_CHANNEL]->toggle();
          }else if (packet->size == 1){
            if (packet->data[0] == 0x00) {
               fanControllers[FAN_BATHROOM_CHANNEL]->turnOff();
            } else if (packet->data[0] == 0x01) {
              fanControllers[FAN_BATHROOM_CHANNEL]->turnOn();
            } else if (packet->data[0] == 0x02) {
              fanControllers[FAN_BATHROOM_CHANNEL]->turnOnWithTimer();
            }
          }
        
        break;
        }
       }
    });
  }

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });


  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    ESP.restart();
  });


  server.on("/t", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    
    struct timeval tp;
    gettimeofday(&tp, 0);
    
    doc["time"] = serialized(String(tp.tv_sec) + String(tp.tv_usec /1000UL));
    for (int i=0; i<ONE_WIRE_NUM_DEVICES; i++){
      if (isValidT(DS18B20_values[i])){
        doc["DS18B20_" + String(i+1)] = serialized(String(DS18B20_values[i], 2));
      }
      
      doc["DS18B20_" + String(i+1)+"_stat"] = serialized(String(DS18B20_values[i], 2)) + " (" + String(DS18B20_errors[i]) + " / " + String(DS18B20_unavailables[i]) + ")";
    }

    String json;
    serializeJson(doc, json);
    
    request->send(200, "application/json", json);
  });

  server.on("/api/temperatures", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    JsonArray sensors = doc.createNestedArray("sensors");
    
    for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
      JsonObject sensorObj = sensors.createNestedObject();
      sensorObj["id"] = formatDeviceId(DS18B20_DEVICES[i]);
      if (isValidT(DS18B20_values[i])) {
        sensorObj["temperature"] = DS18B20_values[i];
      } else {
        sensorObj["temperature"] = nullptr;
      }
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(256);
    appendTimePayload(doc);

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/timezones", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    doc["current"] = currentTimeZoneId;
    JsonArray zones = doc.createNestedArray("zones");

    for (size_t i = 0; i < TIME_ZONES_COUNT; i++) {
      JsonObject zoneObj = zones.createNestedObject();
      zoneObj["id"] = TIME_ZONES[i].id;
      zoneObj["label"] = TIME_ZONES[i].label;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  AsyncCallbackWebHandler* timeZoneHandler = new AsyncCallbackWebHandler();
  timeZoneHandler->setUri("/api/timezone");
  timeZoneHandler->setMethod(HTTP_POST);
  timeZoneHandler->onRequest([](AsyncWebServerRequest *request) {});

  timeZoneHandler->onBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (total > 0 && index == 0) {
      request->_tempObject = malloc(total + 1);
      if (request->_tempObject == NULL) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Недостаточно памяти\"}");
        return;
      }
    }

    if (request->_tempObject) {
      memcpy((uint8_t*)request->_tempObject + index, data, len);

      if (index + len == total) {
        ((uint8_t*)request->_tempObject)[total] = '\0';
        String jsonStr = String((char*)request->_tempObject);

        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, jsonStr);

        bool success = false;
        String message = "";

        if (!error) {
          String tzId = "";
          if (doc.containsKey("id")) {
            tzId = doc["id"].as<String>();
          } else if (doc.containsKey("timeZone")) {
            tzId = doc["timeZone"].as<String>();
          }

          if (tzId.length() > 0) {
            if (applyTimeZoneById(tzId)) {
              saveTimeSettingsToFile();
              success = true;
              message = "Таймзона обновлена";
            } else {
              message = "Неизвестная таймзона";
            }
          } else {
            message = "Отсутствует параметр id";
          }
        } else {
          message = "Ошибка разбора JSON";
        }

        DynamicJsonDocument response(512);
        response["success"] = success;
        response["message"] = message;
        appendTimePayload(response);

        String responseStr;
        serializeJson(response, responseStr);
        request->send(success ? 200 : 400, "application/json", responseStr);

        free(request->_tempObject);
        request->_tempObject = NULL;
      }
    }
  });

  server.addHandler(timeZoneHandler);

  server.on("/ow", HTTP_GET, [](AsyncWebServerRequest *request){
    oneWireEnable(!oneWireEnabled);
    request->send(200, "text/plain", String(oneWireEnabled));
  });

  server.on("/r", HTTP_GET, [](AsyncWebServerRequest *request){
    int i = request->arg("i").toInt();
    relay_state(i, !relay_states[i]);
    
    request->send(200, "text/plain", String(relay_states[i]));
  });

  // API для управления системой теплого пола
  AsyncCallbackWebHandler* heatfloorControlHandler = new AsyncCallbackWebHandler();
  heatfloorControlHandler->setUri("/api/heatfloor/control");
  heatfloorControlHandler->setMethod(HTTP_POST);
  heatfloorControlHandler->onRequest([](AsyncWebServerRequest *request) {});
  
  heatfloorControlHandler->onBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
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
            for (int i = 0; i < HEATING_CHANNELS_NUM; i++) {
              if (channelName == HEATING_CHANNELS_CONFIG[i].name) {
                heatingControllers[i]->setOn(enable);
                saveHeatfloorSettingsToFile(); // Сохраняем новое состояние в файл
                message = String(HEATING_CHANNELS_CONFIG[i].displayName) + (enable ? " включен" : " выключен");
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
  
  server.addHandler(heatfloorControlHandler);
  
  // Эндпоинт для получения состояния вентилятора
  server.on("/api/fan/state", HTTP_GET, [](AsyncWebServerRequest *request){
    // Проверяем наличие параметра канала
    bool success = true;
    int fanIndex = FAN_TOILET_CHANNEL; // По умолчанию используем вентилятор туалета
    FanController* targetFan = nullptr;
    
    if (request->hasArg("channel")) {
      String fanId = request->arg("channel");
      
      // Ищем канал вентилятора по имени
      bool found = false;
      for (int i = 0; i < FAN_CHANNELS_NUM; i++) {
        if (fanId == FAN_CHANNELS_CONFIG[i].name) {
          fanIndex = i;
          found = true;
          break;
        }
      }
      
      if (!found) {
        success = false;
      }
    }
    
    if (success) {
      targetFan = fanControllers[fanIndex];
      
      // Формируем JSON о текущем состоянии конкретного вентилятора
      DynamicJsonDocument doc(128);
      doc["channel"] = FAN_CHANNELS_CONFIG[fanIndex].name;
      doc["name"] = FAN_CHANNELS_CONFIG[fanIndex].displayName;
      doc["on"] = targetFan->getState();
      doc["remainingTime"] = targetFan->getRemainingTime();
      
      String responseStr;
      serializeJson(doc, responseStr);
      request->send(200, "application/json", responseStr);
    } else {
      request->send(404, "application/json", "{\"success\":false,\"message\":\"Вентилятор не найден\"}");
    }
  });
  
  // Обработчик для POST запросов к вентиляторам
  AsyncCallbackWebHandler* newFanHandler = new AsyncCallbackWebHandler();
  newFanHandler->setUri("/api/fan/control");
  newFanHandler->setMethod(HTTP_POST);
  newFanHandler->onRequest([](AsyncWebServerRequest *request) {});
  
  newFanHandler->onBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
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
          int fanIndex = FAN_TOILET_CHANNEL; // По умолчанию - вентилятор туалета
          FanController* targetFan = nullptr;
          
          if (doc.containsKey("channel")) {
            String fanId = doc["channel"].as<String>();
            
            // Ищем канал вентилятора по имени
            bool found = false;
            for (int i = 0; i < FAN_CHANNELS_NUM; i++) {
              if (fanId == FAN_CHANNELS_CONFIG[i].name) {
                fanIndex = i;
                found = true;
                break;
              }
            }
            
            if (!found) {
              message = "Неизвестный канал вентилятора: " + fanId;
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
          
          targetFan = fanControllers[fanIndex];
          String fanName = FAN_CHANNELS_CONFIG[fanIndex].displayName;
          
          if (doc.containsKey("state")) {
            String stateArg = doc["state"].as<String>();
            if (stateArg == "on") {
              targetFan->turnOn();
              message = fanName + " включен";
              success = true;
            } else if (stateArg == "off") {
              targetFan->turnOff();
              message = fanName + " выключен";
              success = true;
            } else if (stateArg == "toggle") {
              targetFan->toggle();
              String newState = targetFan->getState() ? "включен" : "выключен";
              message = fanName + " " + newState;
              success = true;
            } else {
              message = "Неверный параметр состояния";
            }
          } else if (doc.containsKey("timer")) {
            int minutes = doc["timer"].as<int>();
            if (minutes > 0 && minutes <= 180) { // maximum 3 hours
              targetFan->turnOnWithTimer(minutes);
              message = fanName + " включен на " + String(minutes) + " минут";
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
  
  server.addHandler(newFanHandler);
  
  // API для получения текущих настроек теплого пола
  server.on("/api/heatfloor/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    
    // Создаем массив каналов
    JsonArray channels = doc.createNestedArray("channels");
    
    // Добавляем информацию о каждом канале
    for (int i = 0; i < HEATING_CHANNELS_NUM; i++) {
      JsonObject channelObj = channels.createNestedObject();
      
      // Добавляем идентификатор и отображаемое имя канала
      channelObj["name"] = HEATING_CHANNELS_CONFIG[i].name;
      channelObj["displayName"] = HEATING_CHANNELS_CONFIG[i].displayName;
      channelObj["enabled"] = heatingSettings[i].enabled;
      
      // Добавляем расписание
      JsonArray scheduleArray = channelObj.createNestedArray("schedule");
      saveScheduleToJson(scheduleArray, heatingSettings[i].schedule);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API для получения текущего состояния отопления
  server.on("/api/heatfloor/state", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    
    FloorHeatingState _state;
    
    // Формируем JSON с данными всех каналов
    for (int i = 0; i < HEATING_CHANNELS_NUM; i++) {
      JsonObject channelObj = doc.createNestedObject(HEATING_CHANNELS_CONFIG[i].name);
      heatingControllers[i]->getState(&_state);
      
      channelObj["on"] = _state.on;
      if (_state.on) {
        channelObj["relayState"] = _state.relayState;
        channelObj["currentTemperature"] = _state.currentTemperature;
        channelObj["desiredTemperature"] = _state.desiredTemperature;
      }
      channelObj["displayName"] = HEATING_CHANNELS_CONFIG[i].displayName;
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
                for (int i = 0; i < HEATING_CHANNELS_NUM; i++) {
                  if (channelName == HEATING_CHANNELS_CONFIG[i].name) {
                    // Получаем текущие настройки канала
                    FloorHeatingSettings tempSettings = heatingControllers[i]->getSettings();
                    
                    // Обновляем состояние включения
                    if (channelObj.containsKey("enabled")) {
                      tempSettings.enabled = channelObj["enabled"].as<bool>();
                    }
                    
                    // Обновляем расписание
                    if (channelObj.containsKey("schedule") && channelObj["schedule"].is<JsonArray>()) {
                      JsonArray scheduleArray = channelObj["schedule"].as<JsonArray>();
                      std::vector<FloorHeatingSchedule> schedule;
                      loadScheduleFromJson(scheduleArray, schedule);
                      tempSettings.schedule = schedule;
                    }
                    
                    // Применяем настройки
                    heatingControllers[i]->applySettings(tempSettings);
                    
                    // Обновляем глобальную настройку
                    heatingSettings[i] = tempSettings;
                    
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
            saveHeatfloorSettingsToFile();
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
  
  newUpdateSettingsHandler->setUri("/api/heatfloor/settings");
  server.addHandler(newUpdateSettingsHandler);
  
  // Обработчик для корневого маршрута - отдаем index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  // Обработчик для загрузки статических файлов (CSS, JS, изображения)
  server.serveStatic("/", LittleFS, "/");
  
  server.onNotFound( [](AsyncWebServerRequest *request) {
    server_response(request, 404);
  });

  server.begin();

  pinMode(ONE_WIRE_SUPPLY_PIN, OUTPUT);
  applyOneWireEnable();

  for (int i=0; i<ONE_WIRE_NUM_DEVICES; i++){
    DS18B20_values[i] = DEVICE_DISCONNECTED_C;
  }
  DS18B20.begin();
  DS18B20.setResolution(10);

  // Настройка обработчика кнопки для управления вентилятором туалета
  inputs.on(BUTTON_PIN, STATE_LOW, BUTTON_TIMEOUT, [](uint8_t state){
      fanControllers[FAN_TOILET_CHANNEL]->toggle(); // Переключить состояние вентилятора туалета
  }); 
}

bool isValidT(float t){
  return t>-55 && t<125;
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
  long m = millis();
  int p = m % DS18B20_REQUEST_PERIOD;
  if (p == 0){
    int n = (m / DS18B20_REQUEST_PERIOD) % DS18B20_NUM_REQUESTS;
    if (n != prev_n){
      prev_n = n;
      if (n == 0){
        DS18B20.requestTemperatures();
      }else{
        DS18B20_values[n-1] = DS18B20.getTempC(DS18B20_DEVICES[n-1]);
        if (isValidT(DS18B20_values[n-1])){
          DS18B20_last[n-1] = m;
        }else{
          DS18B20_errors[n-1]++;
          if (m-DS18B20_last[n-1] > DS18B20_unavailables[n-1]){
            DS18B20_unavailables[n-1] = m-DS18B20_last[n-1];
          }
        }
      }
    }
  }

  // Обработка всех каналов теплого пола
  for (int i = 0; i < HEATING_CHANNELS_NUM; i++) {
    if (heatingControllers[i] != nullptr) {
      heatingControllers[i]->handle();
    }
  }

  // Обработка таймеров всех вентиляторов
  for (int i = 0; i < FAN_CHANNELS_NUM; i++) {
    if (fanControllers[i] != nullptr) {
      fanControllers[i]->handle();
    }
  }

  inputs.handle();
  
  ArduinoOTA.handle();
  yield();
}
