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
#include <AsyncMqttClient.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include "RelayController.h"
#include "Credentials.h"

#include "Thermostat.h"
#include "Relay.h"
#include "OneWireWatchdog.h"

#include <ESPInputs.h>

#include <ArduinoJson.h>

#include <time.h>
#include <LittleFS.h>  // Используем LittleFS

#include <vector>

Inputs inputs;

// Массивы для настроек и контроллеров теплого пола
ThermostatSettings thermostatSettings[THERMOSTAT_CHANNELS_NUM];
Thermostat* thermostats[THERMOSTAT_CHANNELS_NUM];

// Массив контроллеров вентиляторов
Relay* relays[RELAY_CHANNELS_NUM];

static const long MIN_VALID_EPOCH = 1609459200; // 2021-01-01

bool oneWirePowerEnabled = false;
bool ds18b20NeedsRequest = true;

void publishMqttOneWireState(bool enabled, time_t transitionTs);
void publishMqttButtonEvent(const char* eventType);
void mqttMessageReceived(char* topic, uint8_t* payload, unsigned int length);
void connectMqtt();
void onMqttMessage(
  char* topic,
  char* payload,
  AsyncMqttClientMessageProperties properties,
  size_t len,
  size_t index,
  size_t total
);
void publishMqttStartMessages();
void applyShiftRegisterState();
void setShiftRegisterLedBit(uint8_t* value, uint8_t bitIndex, bool on);
void updateStatusLeds(unsigned long nowMs);
void beginWifiConnection(unsigned long nowMs);

void applyOneWireEnable(bool enabled){
  if (oneWirePowerEnabled != enabled) {
    oneWirePowerEnabled = enabled;
    digitalWrite(ONE_WIRE_SUPPLY_PIN, enabled ? HIGH : LOW);

    if (enabled) {
      // After power restore the sensors need a fresh conversion request.
      ds18b20NeedsRequest = true;
    }

    publishMqttOneWireState(enabled, time(nullptr));
  }
}

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature DS18B20(&oneWire);

struct DS18B20Reading {
  float temperature;
  time_t timestamp;

  bool hasValue() const {
    return timestamp > 0;
  }

  bool hasActualValue() const {
    static const time_t ACTUAL_VALUE_MAX_AGE_SEC = 30 * 60;
    if (!hasValue()) {
      return false;
    }
    time_t now = time(nullptr);
    return now >= timestamp && (now - timestamp) <= ACTUAL_VALUE_MAX_AGE_SEC;
  }
};

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

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip DEVICE_STATIC_IP;
IPAddress gateway DEVICE_GATEWAY_IP;
IPAddress subnet DEVICE_SUBNET_MASK;
IPAddress dnsAddr DEVICE_DNS_IP;

AsyncWebServer server(80); // Порт 80
bool littleFsAvailable = false;

AsyncMqttClient mqttClient;
char mqttIncomingPayloadBuffer[256];
bool mqttWasConnected = false;
uint8_t shiftRegisterState = 0xFF;

static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.nist.gov";
static const char* TIME_SETTINGS_FILE = "/time.json";


static const unsigned long ONE_WIRE_WATCHDOG_RESET_OFF_MS = 30UL * 1000UL;
static const unsigned long ONE_WIRE_WATCHDOG_GRACE_MS = 2 * ONE_WIRE_UPDATE_PERIOD * 1000UL;

static const unsigned long MQTT_RECONNECT_PERIOD_MS = 5000UL;
static const unsigned long WIFI_RECONNECT_PERIOD_MS = 5000UL;
static const unsigned long WIFI_LED_BLINK_PERIOD_MS = 500UL;

unsigned long lastMqttReconnect = 0;
unsigned long lastWifiReconnect = 0;

OneWireWatchdog oneWireWatchdog(
  ONE_WIRE_WATCHDOG_GRACE_MS,
  ONE_WIRE_WATCHDOG_RESET_OFF_MS,
  [](bool enabled) {
    applyOneWireEnable(enabled);
  }
);

String currentTimeZoneId = DEFAULT_TIMEZONE_ID;

void applyShiftRegisterState() {
  digitalWrite(SHIFT_REGISTER_LATCH_PIN, LOW);
  shiftOut(SHIFT_REGISTER_DATA_PIN, SHIFT_REGISTER_CLOCK_PIN, MSBFIRST, shiftRegisterState);
  digitalWrite(SHIFT_REGISTER_LATCH_PIN, HIGH);
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

  uint8_t nextState = 0xFF;
  setShiftRegisterLedBit(&nextState, SHIFT_REGISTER_WIFI_LED_BIT, wifiLedOn);
  for (uint8_t i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
    setShiftRegisterLedBit(&nextState, SHIFT_REGISTER_THERMOSTAT_LED_BITS[i], relay_states[THERMOSTAT_CHANNELS_CONFIG[i].relayPin]);
  }

  if (nextState != shiftRegisterState) {
    shiftRegisterState = nextState;
    applyShiftRegisterState();
  }
}

void beginWifiConnection(unsigned long nowMs) {
  WiFi.config(ip, gateway, subnet, dnsAddr);
  WiFi.begin(ssid, pass);
  lastWifiReconnect = nowMs;
}

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

String mqttSensorMetaPayload(int sensorIndex) {
  String payload = "{";
  payload += "\"type\":\"DS18B20\",";
  payload += "\"units\":{";
  payload += "\"temperature\":\"C\"";
  payload += "},";
  payload += "\"location\":\"" + String(DS18B20_DEVICES_LOCATIONS[sensorIndex]) + "\"";
  payload += "}";
  return payload;
}

String mqttSensorStatePayload(int sensorIndex) {
  String payload = "{";
  payload += "\"temperature\":" + String(DS18B20_values[sensorIndex].temperature, 2) + ",";
  payload += "\"timestamp\":" + String(static_cast<unsigned long>(DS18B20_values[sensorIndex].timestamp));
  payload += "}";
  return payload;
}

String mqttSensorTopicBase(int sensorIndex) {
  return String(MQTT_TOPIC_SENSOR) + "/" + formatDeviceId(DS18B20_DEVICES[sensorIndex]);
}

String mqttSensorMetaTopic(int sensorIndex) {
  return mqttSensorTopicBase(sensorIndex) + "/meta";
}

String mqttSensorStateTopic(int sensorIndex) {
  return mqttSensorTopicBase(sensorIndex) + "/state";
}

String mqttRelayTopicBase(int relayIndex) {
  return String(MQTT_TOPIC_RELAY) + "/" + RELAY_CHANNELS_CONFIG[relayIndex].topicName;
}

String mqttRelayStateTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/state";
}

String mqttRelayMetaTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/meta";
}

String mqttRelaySetOnTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/set/on";
}

String mqttRelaySetOffTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/set/off";
}

String mqttRelaySetToggleTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/set/toggle";
}

String mqttOneWireStatePayload(bool enabled, time_t transitionTs) {
  String payload = "{";
  payload += "\"enabled\":";
  payload += enabled ? "true" : "false";
  if (transitionTs > MIN_VALID_EPOCH) {
    payload += ",\"timestamp\":";
    payload += String(static_cast<unsigned long>(transitionTs));
  }
  payload += "}";
  return payload;
}

String mqttRelayStatePayload(const RelayState& state) {
  String payload = "{";
  payload += "\"on\":";
  payload += state.on ? "true" : "false";
  payload += ",\"relayState\":";
  payload += state.relayState ? "true" : "false";
  payload += ",\"remainingTime\":";
  payload += String(state.remainingTime);
  payload += "}";
  return payload;
}

String mqttRelayMetaPayload(int relayIndex) {
  String payload = "{";
  payload += "\"location\":\"" + String(RELAY_CHANNELS_CONFIG[relayIndex].location) + "\"";
  payload += "}";
  return payload;
}

void publishMqttRelayMeta(int relayIndex) {
  if (!mqttClient.connected()) {
    return;
  }

  String topic = mqttRelayMetaTopic(relayIndex);
  String payload = mqttRelayMetaPayload(relayIndex);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

void publishMqttAllRelaysMeta() {
  for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
    publishMqttRelayMeta(i);
  }
}

void publishMqttRelayState(int relayIndex, const RelayState& state) {
  if (!mqttClient.connected() || relays[relayIndex] == nullptr) {
    return;
  }

  String topic = mqttRelayStateTopic(relayIndex);
  String payload = mqttRelayStatePayload(state);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

void publishMqttRelayState(int relayIndex) {
  if (relays[relayIndex] == nullptr) {
    return;
  }
  publishMqttRelayState(relayIndex, relays[relayIndex]->getState());
}

void publishMqttAllRelaysState() {
  for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
    publishMqttRelayState(i);
  }
}

String mqttThermostatTopicBase(int channelIndex) {
  return String(MQTT_TOPIC_THERMOSTAT) + "/" + THERMOSTAT_CHANNELS_CONFIG[channelIndex].topicName;
}

String mqttThermostatStateTopic(int channelIndex) {
  return mqttThermostatTopicBase(channelIndex) + "/state";
}

String mqttThermostatMetaTopic(int channelIndex) {
  return mqttThermostatTopicBase(channelIndex) + "/meta";
}

String mqttThermostatMetaPayload(int channelIndex) {
  String payload = "{";
  payload += "\"location\":\"" + String(THERMOSTAT_CHANNELS_CONFIG[channelIndex].location) + "\"";
  payload += "}";
  return payload;
}

String mqttThermostatStatePayload(const ThermostatState& state) {
  String payload = "{";
  payload += "\"on\":";
  payload += state.on ? "true" : "false";
  payload += ",\"relayState\":";
  payload += state.relayState ? "true" : "false";
  payload += ",\"currentTemperature\":";
  payload += String(state.currentTemperature, 2);
  payload += ",\"desiredTemperature\":";
  payload += String(state.desiredTemperature, 2);
  payload += "}";
  return payload;
}

void publishMqttThermostatMeta(int channelIndex) {
  if (!mqttClient.connected()) {
    return;
  }

  String topic = mqttThermostatMetaTopic(channelIndex);
  String payload = mqttThermostatMetaPayload(channelIndex);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

void publishMqttAllThermostatMeta() {
  for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
    publishMqttThermostatMeta(i);
  }
}

void publishMqttThermostatState(int channelIndex, const ThermostatState& state) {
  if (!mqttClient.connected()) {
    return;
  }

  String topic = mqttThermostatStateTopic(channelIndex);
  String payload = mqttThermostatStatePayload(state);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

void publishMqttAllThermostatState() {
  for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
    if (thermostats[i] == nullptr) {
      continue;
    }

    ThermostatState state;
    thermostats[i]->getState(&state);
    publishMqttThermostatState(i, state);
  }
}

bool parseRelayDurationMinutes(const uint8_t* payload, unsigned int length, unsigned long& durationMinutes) {
  if (length == 0) {
    return false;
  }

  DynamicJsonDocument doc(128);
  DeserializationError error = deserializeJson(doc, reinterpret_cast<const char*>(payload), length);
  if (error || !doc.containsKey("durationMinutes")) {
    return false;
  }

  long value = doc["durationMinutes"].as<long>();
  if (value <= 0 || value > 180) {
    return false;
  }

  durationMinutes = static_cast<unsigned long>(value);
  return true;
}

void subscribeMqttRelayCommandTopics() {
  for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
    String setOnTopic = mqttRelaySetOnTopic(i);
    String setOffTopic = mqttRelaySetOffTopic(i);
    String setToggleTopic = mqttRelaySetToggleTopic(i);
    mqttClient.subscribe(setOnTopic.c_str(), 1);
    mqttClient.subscribe(setOffTopic.c_str(), 1);
    mqttClient.subscribe(setToggleTopic.c_str(), 1);
  }
}

void mqttMessageReceived(char* topic, uint8_t* payload, unsigned int length) {
  for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
    if (relays[i] == nullptr) {
      continue;
    }

    String setOnTopic = mqttRelaySetOnTopic(i);
    if (setOnTopic == topic) {
      unsigned long durationMinutes = 0;
      if (parseRelayDurationMinutes(payload, length, durationMinutes)) {
        relays[i]->turnOnWithTimer(durationMinutes);
      } else {
        relays[i]->turnOnWithTimer();
      }
      return;
    }

    String setOffTopic = mqttRelaySetOffTopic(i);
    if (setOffTopic == topic) {
      relays[i]->turnOff();
      return;
    }

    String setToggleTopic = mqttRelaySetToggleTopic(i);
    if (setToggleTopic == topic) {
      relays[i]->toggle();
      return;
    }
  }
}

void publishMqttSensorMeta(int sensorIndex) {
  if (!mqttClient.connected()) {
    return;
  }
  String topic = mqttSensorMetaTopic(sensorIndex);
  String payload = mqttSensorMetaPayload(sensorIndex);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

void publishMqttAllSensorsMeta() {
  for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
    publishMqttSensorMeta(i);
  }
}

void publishMqttSensorState(int sensorIndex) {
  if (!mqttClient.connected() || !DS18B20_values[sensorIndex].hasActualValue()) {
    return;
  }

  String topic = mqttSensorStateTopic(sensorIndex);
  String payload = mqttSensorStatePayload(sensorIndex);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

void publishMqttAllSensorsState() {
  for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
    publishMqttSensorState(i);
  }
}

void publishMqttOneWireState(bool enabled, time_t transitionTs) {
  if (!mqttClient.connected()) {
    return;
  }

  String payload = mqttOneWireStatePayload(enabled, transitionTs);
  mqttClient.publish(MQTT_TOPIC_ONEWIRE_STATE, 0, true, payload.c_str());
}

void publishMqttButtonEvent(const char* eventType) {
  if (!mqttClient.connected()) {
    return;
  }

  mqttClient.publish(MQTT_TOPIC_BUTTON_EVENT, 0, false, eventType);
}

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!mqttClient.connected()) {
    mqttClient.connect();
  }
}

void onMqttMessage(
  char* topic,
  char* payload,
  AsyncMqttClientMessageProperties properties,
  size_t len,
  size_t index,
  size_t total
) {
  (void)properties;
  if (topic == nullptr) {
    return;
  }

  if (total == 0) {
    mqttMessageReceived(topic, reinterpret_cast<uint8_t*>(mqttIncomingPayloadBuffer), 0);
    return;
  }

  if (total >= sizeof(mqttIncomingPayloadBuffer)) {
    return;
  }

  if (index + len > total) {
    return;
  }

  memcpy(mqttIncomingPayloadBuffer + index, payload, len);
  if (index + len == total) {
    mqttMessageReceived(topic, reinterpret_cast<uint8_t*>(mqttIncomingPayloadBuffer), total);
  }
}

void publishMqttStartMessages() {
  mqttClient.publish(MQTT_TOPIC_STATUS, 1, true, "online");
  subscribeMqttRelayCommandTopics();
  publishMqttAllSensorsMeta();
  publishMqttAllSensorsState();
  publishMqttAllRelaysMeta();
  publishMqttAllRelaysState();
  publishMqttAllThermostatMeta();
  publishMqttAllThermostatState();
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
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  beginWifiConnection(millis());
  updateStatusLeds(millis());

  ArduinoOTA.setHostname("relay-controller");
  
  ArduinoOTA.onStart([]() {
    // Serial.println("ArduinoOTA start update");
  });

  ArduinoOTA.begin();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setClientId(MQTT_CLIENT_ID);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASSWORD);
  mqttClient.setKeepAlive(30);
  mqttClient.setWill(MQTT_TOPIC_STATUS, 1, true, "offline");
  mqttClient.onMessage(onMqttMessage);
  connectMqtt();

  if (!applyTimeZoneById(currentTimeZoneId)) {
    applyTimeZoneById(getDefaultTimeZone()->id);
  }

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });


  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    ESP.restart();
  });


  server.on("/api/temperatures", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);
    JsonArray sensors = doc.createNestedArray("sensors");
    
    for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
      JsonObject sensorObj = sensors.createNestedObject();
      sensorObj["id"] = formatDeviceId(DS18B20_DEVICES[i]);
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
  applyOneWireEnable(true);

  for (int i=0; i<ONE_WIRE_NUM_DEVICES; i++){
    DS18B20_values[i].temperature = 0;
    DS18B20_values[i].timestamp = 0;
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
  wl_status_t wifiStatus = WiFi.status();

  if (wifiStatus != WL_CONNECTED && (m - lastWifiReconnect) >= WIFI_RECONNECT_PERIOD_MS) {
    beginWifiConnection(m);
    wifiStatus = WiFi.status();
  }

  if (wifiStatus == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      if (m - lastMqttReconnect >= MQTT_RECONNECT_PERIOD_MS) {
        lastMqttReconnect = m;
        connectMqtt();
      }
    }
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
            DS18B20_values[n-1].timestamp = time(nullptr);
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
  
  ArduinoOTA.handle();
  yield();
}
