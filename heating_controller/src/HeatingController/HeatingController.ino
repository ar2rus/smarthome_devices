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
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include "HeatingController.h"
#include "HeatingChannel.h"
#include "QingPingMQTTAdapter.h"
#include "Credentials.h"

#include <ArduinoJson.h>

#include <time.h>
#include <LittleFS.h>

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
    if (now <= 0) {
      return true;
    }
    return now >= timestamp && (now - timestamp) <= ACTUAL_VALUE_MAX_AGE_SEC;
  }
};

struct TemperatureCacheEntry {
  char sensorId[17];
  float temperature;
  unsigned long updatedAtMs;
};

static const unsigned long MQTT_RECONNECT_PERIOD_MS = 5000UL;
static const unsigned long WIFI_RECONNECT_PERIOD_MS = 5000UL;
static const unsigned long WIFI_LED_BLINK_PERIOD_MS = 500UL;
static const unsigned long SHIFT_REGISTER_REFRESH_PERIOD_MS = 1000UL;
static const char* CONFIG_FILE_PATH = "/heating-config.json";
static const unsigned long CHANNEL_SOURCE_MAX_AGE_MS = 30UL * 60UL * 1000UL;
static const size_t CONFIG_JSON_CAPACITY = 16384;
static const char* DEFAULT_CHANNEL_NAMES[RELAY_NUM] = {
  "Лоджия",
  "Спальня",
  "Детская",
  "Гостиная"
};

// Raw 1-Wire values: source for REST /api/onewire/* and MQTT publish.
DS18B20Reading DS18B20_values[ONE_WIRE_NUM_DEVICES];

bool relay_states[RELAY_NUM];

HeatingChannelConfig channelConfigs[RELAY_NUM];
HeatingChannel channelControllers[RELAY_NUM];
ChannelControlMode channelResumeMode[RELAY_NUM];

// MQTT cache (own + external sensors), consumed by HeatingChannel callbacks.
static const uint8_t MAX_TEMPERATURE_CACHE_ENTRIES = 16;
TemperatureCacheEntry temperatureCache[MAX_TEMPERATURE_CACHE_ENTRIES];
char channelSupplySensorStorage[RELAY_NUM][17];
char channelReturnSensorStorage[RELAY_NUM][17];
char channelRoomSensorStorage[RELAY_NUM][MAX_ROOM_GROUP_SIZE][17];

unsigned long lastMqttReconnect = 0;
unsigned long lastWifiReconnect = 0;
bool mqttWasConnected = false;
int prev_n = -1;
bool littleFsReady = false;
uint8_t shiftRegisterState = 0xFF;
unsigned long lastShiftRegisterRefreshMs = 0;

AsyncMqttClient mqttClient;
char mqttIncomingPayloadBuffer[2048];
QingPingMQTTAdapter qingPingAdapter(mqttClient);

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature DS18B20(&oneWire);

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip DEVICE_STATIC_IP;
IPAddress gateway DEVICE_GATEWAY_IP;
IPAddress subnet DEVICE_SUBNET_MASK;
IPAddress dnsAddr DEVICE_DNS_IP;

AsyncWebServer server(80);

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

  uint8_t nextState = 0xFF;
  setShiftRegisterLedBit(&nextState, SHIFT_REGISTER_WIFI_LED_BIT, wifiLedOn);
  for (uint8_t i = 0; i < RELAY_NUM; i++) {
    setShiftRegisterLedBit(&nextState, SHIFT_REGISTER_CHANNEL_LED_BITS[i], relay_states[i]);
  }

  bool stateChanged = nextState != shiftRegisterState;
  if (stateChanged) {
    shiftRegisterState = nextState;
  }

  if (stateChanged || (nowMs - lastShiftRegisterRefreshMs) >= SHIFT_REGISTER_REFRESH_PERIOD_MS) {
    applyShiftRegisterState();
  }
}

void beginWifiConnection(unsigned long nowMs) {
  WiFi.config(ip, gateway, subnet, dnsAddr);
  WiFi.begin(ssid, pass);
  lastWifiReconnect = nowMs;
}

void apply_relay_state(int index) {
  digitalWrite(RELAY_PIN[index], relay_states[index] ? HIGH : LOW);
}

void relay_state(int index, bool on) {
  if (relay_states[index] != on) {
    relay_states[index] = on;
    apply_relay_state(index);
    updateStatusLeds(millis());
  }
}

bool isValidT(float t) {
  return t > -55.0f && t < 85.0f;
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

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!mqttClient.connected()) {
    mqttClient.connect();
  }
}

void publishMqttStartMessages() {
  mqttClient.publish(MQTT_TOPIC_STATUS, 1, true, "online");
  mqttClient.subscribe(MQTT_SOURCE_SENSOR_WILDCARD_TOPIC, 1);
  qingPingAdapter.onMqttConnected(millis());
  publishMqttAllSensorsMeta();
  publishMqttAllSensorsState();
}

void uppercaseAsciiInPlace(char* value) {
  if (value == nullptr) {
    return;
  }
  for (size_t i = 0; value[i] != '\0'; i++) {
    if (value[i] >= 'a' && value[i] <= 'z') {
      value[i] = static_cast<char>(value[i] - ('a' - 'A'));
    }
  }
}

bool normalizeSensorId(const char* sensorId, char outNormalized[17]) {
  if (sensorId == nullptr || outNormalized == nullptr) {
    return false;
  }

  strncpy(outNormalized, sensorId, 16);
  outNormalized[16] = '\0';
  uppercaseAsciiInPlace(outNormalized);
  return outNormalized[0] != '\0';
}

int findCacheEntry(const char* sensorId) {
  char normalized[17];
  if (!normalizeSensorId(sensorId, normalized)) {
    return -1;
  }

  for (int i = 0; i < MAX_TEMPERATURE_CACHE_ENTRIES; i++) {
    if (temperatureCache[i].sensorId[0] != '\0' && strcmp(temperatureCache[i].sensorId, normalized) == 0) {
      return i;
    }
  }
  return -1;
}

int allocateCacheEntry(const char* sensorId) {
  char normalized[17];
  if (!normalizeSensorId(sensorId, normalized)) {
    return -1;
  }

  for (int i = 0; i < MAX_TEMPERATURE_CACHE_ENTRIES; i++) {
    if (temperatureCache[i].sensorId[0] == '\0') {
      strncpy(temperatureCache[i].sensorId, normalized, sizeof(temperatureCache[i].sensorId) - 1);
      temperatureCache[i].sensorId[sizeof(temperatureCache[i].sensorId) - 1] = '\0';
      temperatureCache[i].temperature = 0.0f;
      temperatureCache[i].updatedAtMs = 0;
      return i;
    }
  }
  return -1;
}

bool extractSensorIdFromTopic(const char* topic, char outSensorId[17]) {
  if (topic == nullptr || outSensorId == nullptr) {
    return false;
  }

  const char* marker = strstr(topic, "/sensor/");
  if (marker == nullptr) {
    return false;
  }
  marker += 8;

  const char* suffix = strstr(marker, "/state");
  if (suffix == nullptr) {
    return false;
  }

  size_t len = static_cast<size_t>(suffix - marker);
  if (len == 0 || len >= 17) {
    return false;
  }

  memcpy(outSensorId, marker, len);
  outSensorId[len] = '\0';
  uppercaseAsciiInPlace(outSensorId);

  return true;
}

bool parseTemperaturePayload(
  const uint8_t* payload,
  unsigned int length,
  float* outValue,
  unsigned long* outUpdatedAtMs
) {
  if (outValue == nullptr || outUpdatedAtMs == nullptr) {
    return false;
  }

  String body;
  body.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    body += static_cast<char>(payload[i]);
  }
  body.trim();

  if (body.length() == 0 || body.equalsIgnoreCase("null")) {
    return false;
  }

  if (body.charAt(0) == '{') {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
      return false;
    }

    JsonVariant tempVar = doc["temperature"];
    if (tempVar.isNull()) {
      return false;
    }

    float value = tempVar.as<float>();
    if (!isValidT(value)) {
      return false;
    }

    unsigned long updatedAtMs = millis();
    JsonVariant tsVar = doc["timestamp"];
    if (!tsVar.isNull()) {
      time_t nowTs = time(nullptr);
      time_t sourceTs = static_cast<time_t>(tsVar.as<unsigned long>());
      if (nowTs > 0 && sourceTs > 0 && nowTs >= sourceTs) {
        unsigned long ageMs = static_cast<unsigned long>(nowTs - sourceTs) * 1000UL;
        unsigned long nowMs = millis();
        updatedAtMs = nowMs - ageMs;
      }
    }

    *outValue = value;
    *outUpdatedAtMs = updatedAtMs;
    return true;
  }

  char* endPtr = nullptr;
  float value = strtof(body.c_str(), &endPtr);
  if (endPtr == body.c_str()) {
    return false;
  }

  if (!isValidT(value)) {
    return false;
  }

  *outValue = value;
  *outUpdatedAtMs = millis();
  return true;
}

void mqttMessageReceived(char* topic, const uint8_t* payload, unsigned int length) {
  char sensorId[17];
  if (!extractSensorIdFromTopic(topic, sensorId)) {
    return;
  }

  float value = 0.0f;
  unsigned long updatedAtMs = 0;
  if (!parseTemperaturePayload(payload, length, &value, &updatedAtMs)) {
    return;
  }

  int index = findCacheEntry(sensorId);
  if (index < 0) {
    index = allocateCacheEntry(sensorId);
  }
  if (index < 0) {
    return;
  }

  temperatureCache[index].temperature = value;
  temperatureCache[index].updatedAtMs = updatedAtMs;
}

void dispatchMqttMessage(char* topic, const uint8_t* payload, unsigned int length) {
  if (qingPingAdapter.handleMqttMessage(topic, payload, length)) {
    return;
  }
  mqttMessageReceived(topic, payload, length);
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
    dispatchMqttMessage(topic, reinterpret_cast<uint8_t*>(mqttIncomingPayloadBuffer), 0);
    return;
  }

  if (total > sizeof(mqttIncomingPayloadBuffer)) {
    return;
  }

  if (index + len > total) {
    return;
  }

  memcpy(mqttIncomingPayloadBuffer + index, payload, len);
  if (index + len == total) {
    dispatchMqttMessage(topic, reinterpret_cast<uint8_t*>(mqttIncomingPayloadBuffer), total);
  }
}

bool temperatureLookupFromCache(
  const char* sensorId,
  float* outTemperature,
  unsigned long* outUpdatedAtMs
) {
  if (sensorId == nullptr || outTemperature == nullptr || outUpdatedAtMs == nullptr) {
    return false;
  }

  int index = findCacheEntry(sensorId);
  if (index < 0) {
    return false;
  }

  *outTemperature = temperatureCache[index].temperature;
  *outUpdatedAtMs = temperatureCache[index].updatedAtMs;
  return true;
}

void relayControlCallback(uint8_t channelIndex, bool on) {
  relay_state(channelIndex, on);
}

void setSensorIdStorage(char storage[17], const char* sensorId) {
  if (storage == nullptr || sensorId == nullptr) {
    if (storage != nullptr) {
      storage[0] = '\0';
    }
    return;
  }

  char normalized[17];
  if (!normalizeSensorId(sensorId, normalized)) {
    storage[0] = '\0';
    return;
  }

  memcpy(storage, normalized, 17);
}

void setChannelName(char storage[32], const char* name) {
  if (storage == nullptr) {
    return;
  }
  if (name == nullptr) {
    storage[0] = '\0';
    return;
  }
  strncpy(storage, name, 31);
  storage[31] = '\0';
}

const char* sensorIdFromStorage(const char storage[17]) {
  return (storage != nullptr && storage[0] != '\0') ? storage : nullptr;
}

bool isValidChannelModeValue(int value) {
  return value == CHANNEL_CONTROL_MODE_AUTO_PWM ||
    value == CHANNEL_CONTROL_MODE_PWM_PERCENT ||
    value == CHANNEL_CONTROL_MODE_OFF ||
    value == CHANNEL_CONTROL_MODE_ON ||
    value == CHANNEL_CONTROL_MODE_AUTO;
}

ChannelControlMode normalizedResumeMode(ChannelControlMode mode) {
  if (mode == CHANNEL_CONTROL_MODE_OFF) {
    return CHANNEL_CONTROL_MODE_AUTO;
  }
  return mode;
}

void refreshChannelSensorPointers(uint8_t channelIndex) {
  HeatingChannelConfig& cfg = channelConfigs[channelIndex];
  cfg.supplySensorId = sensorIdFromStorage(channelSupplySensorStorage[channelIndex]);
  cfg.returnSensorId = sensorIdFromStorage(channelReturnSensorStorage[channelIndex]);
  for (uint8_t j = 0; j < MAX_ROOM_GROUP_SIZE; j++) {
    if (j < cfg.roomSensorCount) {
      cfg.roomSensorIds[j] = sensorIdFromStorage(channelRoomSensorStorage[channelIndex][j]);
    } else {
      cfg.roomSensorIds[j] = nullptr;
    }
  }
}

void sanitizeChannelConfig(uint8_t channelIndex) {
  HeatingChannelConfig& cfg = channelConfigs[channelIndex];

  if (cfg.roomSensorCount > MAX_ROOM_GROUP_SIZE) {
    cfg.roomSensorCount = MAX_ROOM_GROUP_SIZE;
  }

  if (cfg.pwmPeriodSec == 0) {
    cfg.pwmPeriodSec = 1;
  }
  if (cfg.tonMaxSec > cfg.pwmPeriodSec) {
    cfg.tonMaxSec = cfg.pwmPeriodSec;
  }
  if (cfg.tonMinSec > cfg.tonMaxSec) {
    cfg.tonMinSec = cfg.tonMaxSec;
  }
  if (cfg.stepSec == 0) {
    cfg.stepSec = 1;
  }
  if (cfg.tonSafeSec > cfg.pwmPeriodSec) {
    cfg.tonSafeSec = cfg.pwmPeriodSec;
  }

  if (cfg.manualPercent < 0.0f) {
    cfg.manualPercent = 0.0f;
  }
  if (cfg.manualPercent > 100.0f) {
    cfg.manualPercent = 100.0f;
  }

  if (cfg.targetRoomIndex >= cfg.roomSensorCount) {
    cfg.targetRoomIndex = 0;
  }

  if (cfg.channelName[0] == '\0') {
    char fallback[32];
    snprintf(fallback, sizeof(fallback), "Channel %u", static_cast<unsigned>(channelIndex + 1));
    setChannelName(cfg.channelName, fallback);
  }

  refreshChannelSensorPointers(channelIndex);
}

void applyChannelController(uint8_t channelIndex, unsigned long nowMs) {
  channelControllers[channelIndex].configure(
    channelIndex,
    channelConfigs[channelIndex],
    temperatureLookupFromCache,
    relayControlCallback,
    CHANNEL_SOURCE_MAX_AGE_MS
  );
  channelControllers[channelIndex].begin(nowMs);
  channelControllers[channelIndex].handle(nowMs);
}

void applyAllChannelControllers(unsigned long nowMs) {
  for (uint8_t i = 0; i < RELAY_NUM; i++) {
    applyChannelController(i, nowMs);
  }
}

void appendChannelConfigJson(JsonObject channel, uint8_t index) {
  const HeatingChannelConfig& cfg = channelConfigs[index];

  channel["index"] = index;
  channel["channelName"] = cfg.channelName;
  channel["mode"] = static_cast<uint8_t>(cfg.mode);
  channel["resumeMode"] = static_cast<uint8_t>(channelResumeMode[index]);
  channel["tSet"] = cfg.tSet;
  channel["hRoom"] = cfg.hRoom;
  channel["pwmPeriodSec"] = cfg.pwmPeriodSec;
  channel["tonMinSec"] = cfg.tonMinSec;
  channel["tonMaxSec"] = cfg.tonMaxSec;
  channel["stepSec"] = cfg.stepSec;
  channel["roomTemperatureStrategy"] = static_cast<uint8_t>(cfg.roomTemperatureStrategy);
  channel["targetRoomIndex"] = cfg.targetRoomIndex;
  channel["offsetSupply"] = cfg.offsetSupply;
  channel["offsetReturn"] = cfg.offsetReturn;
  channel["tauRoomSec"] = cfg.tauRoomSec;
  channel["tauSupplySec"] = cfg.tauSupplySec;
  channel["tauReturnSec"] = cfg.tauReturnSec;
  channel["tRetMax"] = cfg.tRetMax;
  channel["hRet"] = cfg.hRet;
  channel["deltaTMin"] = cfg.deltaTMin;
  channel["manualPercent"] = cfg.manualPercent;
  channel["manualWaterLimitsEnable"] = cfg.manualWaterLimitsEnable;
  channel["tonSafeSec"] = cfg.tonSafeSec;
  channel["trendGuardEnable"] = cfg.trendGuardEnable;

  if (cfg.supplySensorId != nullptr) {
    channel["supplySensorId"] = cfg.supplySensorId;
  } else {
    channel["supplySensorId"] = nullptr;
  }

  if (cfg.returnSensorId != nullptr) {
    channel["returnSensorId"] = cfg.returnSensorId;
  } else {
    channel["returnSensorId"] = nullptr;
  }

  channel["roomSensorCount"] = cfg.roomSensorCount;

  JsonArray roomSensors = channel.createNestedArray("roomSensorIds");
  for (uint8_t j = 0; j < cfg.roomSensorCount && j < MAX_ROOM_GROUP_SIZE; j++) {
    if (cfg.roomSensorIds[j] != nullptr) {
      roomSensors.add(cfg.roomSensorIds[j]);
    } else {
      roomSensors.add(nullptr);
    }
  }

  JsonArray offsetRoom = channel.createNestedArray("offsetRoom");
  for (uint8_t j = 0; j < MAX_ROOM_GROUP_SIZE; j++) {
    offsetRoom.add(cfg.offsetRoom[j]);
  }
}

bool applyChannelConfigPatchFromJson(uint8_t channelIndex, JsonObjectConst patch) {
  if (channelIndex >= RELAY_NUM) {
    return false;
  }

  HeatingChannelConfig& cfg = channelConfigs[channelIndex];
  bool changed = false;

  if (patch.containsKey("mode")) {
    int value = patch["mode"].as<int>();
    if (isValidChannelModeValue(value)) {
      cfg.mode = static_cast<ChannelControlMode>(value);
      if (cfg.mode != CHANNEL_CONTROL_MODE_OFF) {
        channelResumeMode[channelIndex] = normalizedResumeMode(cfg.mode);
      }
      changed = true;
    }
  }
  if (patch.containsKey("resumeMode")) {
    int value = patch["resumeMode"].as<int>();
    if (isValidChannelModeValue(value)) {
      ChannelControlMode parsed = static_cast<ChannelControlMode>(value);
      if (parsed != CHANNEL_CONTROL_MODE_OFF) {
        channelResumeMode[channelIndex] = normalizedResumeMode(parsed);
        changed = true;
      }
    }
  }
  if (patch.containsKey("channelName")) {
    JsonVariantConst value = patch["channelName"];
    if (value.isNull()) {
      cfg.channelName[0] = '\0';
    } else {
      setChannelName(cfg.channelName, value.as<const char*>());
    }
    changed = true;
  }
  if (patch.containsKey("tSet")) { cfg.tSet = patch["tSet"].as<float>(); changed = true; }
  if (patch.containsKey("hRoom")) { cfg.hRoom = patch["hRoom"].as<float>(); changed = true; }
  if (patch.containsKey("pwmPeriodSec")) { cfg.pwmPeriodSec = patch["pwmPeriodSec"].as<uint16_t>(); changed = true; }
  if (patch.containsKey("tonMinSec")) { cfg.tonMinSec = patch["tonMinSec"].as<uint16_t>(); changed = true; }
  if (patch.containsKey("tonMaxSec")) { cfg.tonMaxSec = patch["tonMaxSec"].as<uint16_t>(); changed = true; }
  if (patch.containsKey("stepSec")) { cfg.stepSec = patch["stepSec"].as<uint16_t>(); changed = true; }

  if (patch.containsKey("roomTemperatureStrategy")) {
    int value = patch["roomTemperatureStrategy"].as<int>();
    if (value >= ROOM_TEMPERATURE_STRATEGY_MIN && value <= ROOM_TEMPERATURE_STRATEGY_MEDIAN) {
      cfg.roomTemperatureStrategy = static_cast<RoomTemperatureStrategy>(value);
      changed = true;
    }
  }
  if (patch.containsKey("targetRoomIndex")) { cfg.targetRoomIndex = patch["targetRoomIndex"].as<uint8_t>(); changed = true; }

  if (patch.containsKey("supplySensorId")) {
    JsonVariantConst value = patch["supplySensorId"];
    if (value.isNull()) {
      channelSupplySensorStorage[channelIndex][0] = '\0';
    } else {
      setSensorIdStorage(channelSupplySensorStorage[channelIndex], value.as<const char*>());
    }
    changed = true;
  }

  if (patch.containsKey("returnSensorId")) {
    JsonVariantConst value = patch["returnSensorId"];
    if (value.isNull()) {
      channelReturnSensorStorage[channelIndex][0] = '\0';
    } else {
      setSensorIdStorage(channelReturnSensorStorage[channelIndex], value.as<const char*>());
    }
    changed = true;
  }

  if (patch.containsKey("roomSensorIds")) {
    for (uint8_t j = 0; j < MAX_ROOM_GROUP_SIZE; j++) {
      channelRoomSensorStorage[channelIndex][j][0] = '\0';
    }

    JsonArrayConst values = patch["roomSensorIds"].as<JsonArrayConst>();
    uint8_t count = 0;
    for (JsonVariantConst value : values) {
      if (count >= MAX_ROOM_GROUP_SIZE) {
        break;
      }
      if (!value.isNull()) {
        setSensorIdStorage(channelRoomSensorStorage[channelIndex][count], value.as<const char*>());
        if (channelRoomSensorStorage[channelIndex][count][0] != '\0') {
          count++;
        }
      }
    }
    cfg.roomSensorCount = count;
    changed = true;
  } else if (patch.containsKey("roomSensorCount")) {
    cfg.roomSensorCount = patch["roomSensorCount"].as<uint8_t>();
    changed = true;
  }

  if (patch.containsKey("offsetSupply")) { cfg.offsetSupply = patch["offsetSupply"].as<float>(); changed = true; }
  if (patch.containsKey("offsetReturn")) { cfg.offsetReturn = patch["offsetReturn"].as<float>(); changed = true; }
  if (patch.containsKey("tauRoomSec")) { cfg.tauRoomSec = patch["tauRoomSec"].as<float>(); changed = true; }
  if (patch.containsKey("tauSupplySec")) { cfg.tauSupplySec = patch["tauSupplySec"].as<float>(); changed = true; }
  if (patch.containsKey("tauReturnSec")) { cfg.tauReturnSec = patch["tauReturnSec"].as<float>(); changed = true; }
  if (patch.containsKey("tRetMax")) { cfg.tRetMax = patch["tRetMax"].as<float>(); changed = true; }
  if (patch.containsKey("hRet")) { cfg.hRet = patch["hRet"].as<float>(); changed = true; }
  if (patch.containsKey("deltaTMin")) { cfg.deltaTMin = patch["deltaTMin"].as<float>(); changed = true; }
  if (patch.containsKey("manualPercent")) { cfg.manualPercent = patch["manualPercent"].as<float>(); changed = true; }
  if (patch.containsKey("manualWaterLimitsEnable")) { cfg.manualWaterLimitsEnable = patch["manualWaterLimitsEnable"].as<bool>(); changed = true; }
  if (patch.containsKey("tonSafeSec")) { cfg.tonSafeSec = patch["tonSafeSec"].as<uint16_t>(); changed = true; }
  if (patch.containsKey("trendGuardEnable")) { cfg.trendGuardEnable = patch["trendGuardEnable"].as<bool>(); changed = true; }

  if (patch.containsKey("offsetRoom")) {
    JsonArrayConst values = patch["offsetRoom"].as<JsonArrayConst>();
    for (uint8_t j = 0; j < MAX_ROOM_GROUP_SIZE; j++) {
      cfg.offsetRoom[j] = 0.0f;
    }
    uint8_t j = 0;
    for (JsonVariantConst value : values) {
      if (j >= MAX_ROOM_GROUP_SIZE) {
        break;
      }
      cfg.offsetRoom[j++] = value.as<float>();
    }
    changed = true;
  }

  if (!changed) {
    return false;
  }

  sanitizeChannelConfig(channelIndex);
  return true;
}

bool saveConfigToFs() {
  if (!littleFsReady) {
    return false;
  }

  DynamicJsonDocument doc(CONFIG_JSON_CAPACITY);
  doc["version"] = 1;
  JsonArray channels = doc.createNestedArray("channels");
  for (uint8_t i = 0; i < RELAY_NUM; i++) {
    JsonObject channel = channels.createNestedObject();
    appendChannelConfigJson(channel, i);
  }

  File file = LittleFS.open(CONFIG_FILE_PATH, "w");
  if (!file) {
    return false;
  }

  size_t bytes = serializeJson(doc, file);
  file.close();
  return bytes > 0;
}

bool loadConfigFromFs() {
  if (!littleFsReady || !LittleFS.exists(CONFIG_FILE_PATH)) {
    return false;
  }

  File file = LittleFS.open(CONFIG_FILE_PATH, "r");
  if (!file) {
    return false;
  }

  DynamicJsonDocument doc(CONFIG_JSON_CAPACITY);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    return false;
  }

  JsonArrayConst channels = doc["channels"].as<JsonArrayConst>();
  if (channels.isNull()) {
    return false;
  }

  uint8_t arrayIndex = 0;
  for (JsonObjectConst channel : channels) {
    uint8_t channelIndex = channel["index"] | arrayIndex;
    if (channelIndex < RELAY_NUM) {
      applyChannelConfigPatchFromJson(channelIndex, channel);
    }
    arrayIndex++;
  }

  return true;
}

void initChannelConfigDefaults() {
  for (int i = 0; i < RELAY_NUM; i++) {
    channelConfigs[i] = DEFAULT_HEATING_CHANNEL_CONFIG;
    channelResumeMode[i] = normalizedResumeMode(channelConfigs[i].mode);
    setChannelName(channelConfigs[i].channelName, DEFAULT_CHANNEL_NAMES[i]);

    setSensorIdStorage(channelSupplySensorStorage[i], CHANNEL_SUPPLY_SENSOR_IDS[i]);
    setSensorIdStorage(channelReturnSensorStorage[i], CHANNEL_RETURN_SENSOR_IDS[i]);

    channelConfigs[i].offsetSupply = CHANNEL_OFFSET_SUPPLY;
    channelConfigs[i].offsetReturn = CHANNEL_OFFSET_RETURN[i];

    uint8_t roomSensorCount = CHANNEL_ROOM_SENSOR_COUNTS[i];
    if (roomSensorCount > MAX_ROOM_GROUP_SIZE) {
      roomSensorCount = MAX_ROOM_GROUP_SIZE;
    }
    channelConfigs[i].roomSensorCount = roomSensorCount;

    for (int j = 0; j < MAX_ROOM_GROUP_SIZE; j++) {
      setSensorIdStorage(channelRoomSensorStorage[i][j], CHANNEL_ROOM_SENSOR_IDS[i][j]);
      channelConfigs[i].offsetRoom[j] = CHANNEL_OFFSET_ROOM[i][j];
    }

    channelConfigs[i].roomTemperatureStrategy = ROOM_TEMPERATURE_STRATEGY_MIN;
    channelConfigs[i].targetRoomIndex = 0;
    sanitizeChannelConfig(i);
    if (channelConfigs[i].mode != CHANNEL_CONTROL_MODE_OFF) {
      channelResumeMode[i] = normalizedResumeMode(channelConfigs[i].mode);
    }
  }
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
      request->send(404, "text/plain", "File Not Found\n\n");
      break;
  }
}

void sendCurrentConfig(AsyncWebServerRequest* request) {
  DynamicJsonDocument doc(8192);
  doc["success"] = true;
  doc["relayCount"] = RELAY_NUM;
  doc["maxRoomGroupSize"] = MAX_ROOM_GROUP_SIZE;
  doc["littleFsReady"] = littleFsReady;
  doc["configPath"] = CONFIG_FILE_PATH;

  JsonArray channels = doc.createNestedArray("channels");
  for (uint8_t i = 0; i < RELAY_NUM; i++) {
    JsonObject channel = channels.createNestedObject();
    appendChannelConfigJson(channel, i);
  }

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

void handleConfigChannelBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  if (index == 0) {
    String* body = new String();
    body->reserve(total + 1);
    request->_tempObject = body;
  }

  String* body = reinterpret_cast<String*>(request->_tempObject);
  if (body == nullptr) {
    request->send(500, "text/plain", "Internal error\n");
    return;
  }

  for (size_t i = 0; i < len; i++) {
    body->concat(static_cast<char>(data[i]));
  }

  if (index + len != total) {
    return;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (error) {
    request->send(400, "text/plain", "Invalid JSON\n");
    return;
  }

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull() || !root.containsKey("index")) {
    request->send(400, "text/plain", "Missing index\n");
    return;
  }

  int indexValue = root["index"].as<int>();
  if (indexValue < 0 || indexValue >= RELAY_NUM) {
    request->send(400, "text/plain", "Invalid index\n");
    return;
  }

  uint8_t channelIndex = static_cast<uint8_t>(indexValue);
  JsonObjectConst rootConst = root;
  bool changed = applyChannelConfigPatchFromJson(channelIndex, rootConst);
  if (!changed) {
    DynamicJsonDocument response(512);
    response["success"] = true;
    response["changed"] = false;
    response["saved"] = false;
    String json;
    serializeJson(response, json);
    request->send(200, "application/json", json);
    return;
  }

  applyChannelController(channelIndex, millis());
  bool saved = saveConfigToFs();

  DynamicJsonDocument response(2048);
  response["success"] = true;
  response["changed"] = true;
  response["saved"] = saved;
  JsonObject channel = response.createNestedObject("channel");
  appendChannelConfigJson(channel, channelIndex);
  String json;
  serializeJson(response, json);
  request->send(200, "application/json", json);
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

  for (int i = 0; i < RELAY_NUM; i++) {
    relay_states[i] = false;
    pinMode(RELAY_PIN[i], OUTPUT);
    apply_relay_state(i);
  }
  updateStatusLeds(millis());

  for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
    DS18B20_values[i].temperature = 0.0f;
    DS18B20_values[i].timestamp = 0;
  }

  for (int i = 0; i < MAX_TEMPERATURE_CACHE_ENTRIES; i++) {
    temperatureCache[i].sensorId[0] = '\0';
    temperatureCache[i].temperature = 0.0f;
    temperatureCache[i].updatedAtMs = 0;
  }

  littleFsReady = LittleFS.begin();
  if (!littleFsReady) {
    // Serial.println("Failed to mount LittleFS");
  }

  initChannelConfigDefaults();
  bool loadedConfig = loadConfigFromFs();
  if (!loadedConfig) {
    if (!LittleFS.exists(CONFIG_FILE_PATH)) {
      saveConfigToFs();
    } else {
      // Serial.println("Config file exists but failed to load; keep runtime defaults");
    }
  }
  applyAllChannelControllers(millis());
  updateStatusLeds(millis());

  DS18B20.begin();
  DS18B20.setResolution(10);
  DS18B20.requestTemperatures();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  beginWifiConnection(millis());
  updateStatusLeds(millis());

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setClientId(MQTT_CLIENT_ID);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASSWORD);
  mqttClient.setKeepAlive(30);
  mqttClient.setWill(MQTT_TOPIC_STATUS, 1, true, "offline");
  mqttClient.onMessage(onMqttMessage);
  connectMqtt();

  ArduinoOTA.setHostname("heating-controller");
  ArduinoOTA.onStart([]() {
    // Serial.println("ArduinoOTA start update");
  });
  ArduinoOTA.begin();

  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    ESP.restart();
  });

  server.on("/api/relay/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(9216);
    JsonArray channels = doc.createNestedArray("channels");
    unsigned long nowMs = millis();
    time_t nowTs = time(nullptr);

    bool hasInput = false;
    float inputTemp = 0.0f;

    doc["uptimeMs"] = nowMs;
    doc["heap"] = ESP.getFreeHeap();
    doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    doc["mqttConnected"] = mqttClient.connected();
    doc["littleFsReady"] = littleFsReady;

    for (int i = 0; i < RELAY_NUM; i++) {
      JsonObject channel = channels.createNestedObject();
      const HeatingChannelState& st = channelControllers[i].state();
      const HeatingChannelConfig& cfg = channelControllers[i].config();

      channel["index"] = i;
      channel["on"] = st.relayOn;
      channel["elapsedMs"] = nowMs - st.relayStateChangedAtMs;
      channel["mode"] = static_cast<int>(channelControllers[i].mode());
      channel["tonSec"] = st.tonSec;
      channel["pwmPeriodSec"] = cfg.pwmPeriodSec;
      channel["tSet"] = cfg.tSet;
      channel["manualPercent"] = cfg.manualPercent;
      channel["faultWarning"] = st.faultWarning;

      if (st.hasReturnTemp) {
        channel["returnTemp"] = st.returnTemp;
      } else {
        channel["returnTemp"] = nullptr;
      }

      if (st.hasSupplyTemp) {
        channel["supplyTemp"] = st.supplyTemp;
      } else {
        channel["supplyTemp"] = nullptr;
      }

      if (st.hasRoomTemp) {
        channel["roomTemp"] = st.roomTemp;
      } else {
        channel["roomTemp"] = nullptr;
      }

      JsonArray roomSensors = channel.createNestedArray("roomSensors");
      for (uint8_t j = 0; j < st.roomSensorCount && j < MAX_ROOM_GROUP_SIZE; j++) {
        JsonObject roomSensor = roomSensors.createNestedObject();
        if (cfg.roomSensorIds[j] != nullptr) {
          roomSensor["id"] = cfg.roomSensorIds[j];
        } else {
          roomSensor["id"] = nullptr;
        }
        if (st.hasRoomSensorTemp[j]) {
          roomSensor["temperature"] = st.roomSensorTemp[j];
        } else {
          roomSensor["temperature"] = nullptr;
        }
        if (st.hasRoomSensorRawTemp[j]) {
          roomSensor["rawTemperature"] = st.roomSensorRawTemp[j];
        } else {
          roomSensor["rawTemperature"] = nullptr;
        }
      }

      if (!hasInput && st.hasSupplyTemp) {
        hasInput = true;
        inputTemp = st.supplyTemp;
      }
    }

    JsonArray oneWire = doc.createNestedArray("oneWire");
    for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
      JsonObject sensor = oneWire.createNestedObject();
      sensor["id"] = formatDeviceId(DS18B20_DEVICES[i]);
      sensor["timestamp"] = static_cast<unsigned long>(DS18B20_values[i].timestamp);
      if (DS18B20_values[i].hasActualValue()) {
        sensor["temperature"] = DS18B20_values[i].temperature;
      } else {
        sensor["temperature"] = nullptr;
      }
      if (nowTs > 0 && DS18B20_values[i].timestamp > 0 && nowTs >= DS18B20_values[i].timestamp) {
        sensor["ageSec"] = static_cast<unsigned long>(nowTs - DS18B20_values[i].timestamp);
      } else {
        sensor["ageSec"] = nullptr;
      }
    }

    JsonArray mqttCache = doc.createNestedArray("mqttCache");
    for (int i = 0; i < MAX_TEMPERATURE_CACHE_ENTRIES; i++) {
      if (temperatureCache[i].sensorId[0] == '\0') {
        continue;
      }
      JsonObject sensor = mqttCache.createNestedObject();
      sensor["id"] = temperatureCache[i].sensorId;
      sensor["temperature"] = temperatureCache[i].temperature;
      sensor["updatedAtMs"] = temperatureCache[i].updatedAtMs;
      if (nowMs >= temperatureCache[i].updatedAtMs) {
        sensor["ageMs"] = nowMs - temperatureCache[i].updatedAtMs;
      } else {
        sensor["ageMs"] = nullptr;
      }
    }

    if (hasInput) {
      doc["inputTemp"] = inputTemp;
    } else {
      doc["inputTemp"] = nullptr;
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/relay/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasArg("index")) {
      request->send(400, "text/plain", "Missing index\n");
      return;
    }

    int i = request->arg("index").toInt();
    if (i < 0 || i >= RELAY_NUM) {
      request->send(400, "text/plain", "Invalid index\n");
      return;
    }

    ChannelControlMode mode = channelControllers[i].mode();
    if (mode == CHANNEL_CONTROL_MODE_OFF) {
      channelConfigs[i].mode = normalizedResumeMode(channelResumeMode[i]);
    } else {
      channelResumeMode[i] = normalizedResumeMode(mode);
      channelConfigs[i].mode = CHANNEL_CONTROL_MODE_OFF;
    }

    sanitizeChannelConfig(i);
    applyChannelController(i, millis());
    bool saved = saveConfigToFs();

    DynamicJsonDocument doc(512);
    doc["success"] = true;
    doc["index"] = i;
    doc["on"] = relay_states[i];
    doc["mode"] = static_cast<int>(channelControllers[i].mode());
    doc["elapsedMs"] = millis() - channelControllers[i].state().relayStateChangedAtMs;
    doc["saved"] = saved;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendCurrentConfig(request);
  });

  server.on("/api/config/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (littleFsReady && LittleFS.exists(CONFIG_FILE_PATH)) {
      request->send(LittleFS, CONFIG_FILE_PATH, "application/json", true);
      return;
    }

    DynamicJsonDocument doc(8192);
    doc["version"] = 1;
    JsonArray channels = doc.createNestedArray("channels");
    for (uint8_t i = 0; i < RELAY_NUM; i++) {
      JsonObject channel = channels.createNestedObject();
      appendChannelConfigJson(channel, i);
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/config/channel", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      (void)request;
    },
    nullptr,
    handleConfigChannelBody
  );

  server.on("/api/config/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["saved"] = saveConfigToFs();
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/config/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    initChannelConfigDefaults();
    applyAllChannelControllers(millis());
    bool saved = saveConfigToFs();

    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["saved"] = saved;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/onewire/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    JsonArray devices = doc.createNestedArray("devices");

    uint8_t addr[8];
    oneWire.reset_search();
    while (oneWire.search(addr)) {
      if (OneWire::crc8(addr, 7) != addr[7]) {
        continue;
      }
      devices.add(formatDeviceId(addr));
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/onewire/values", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    JsonObject values = doc.createNestedObject("values");

    for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
      String id = formatDeviceId(DS18B20_DEVICES[i]);
      if (DS18B20_values[i].hasActualValue()) {
        values[id] = DS18B20_values[i].temperature;
      } else {
        values[id] = nullptr;
      }
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  if (littleFsReady) {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(LittleFS, "/index.html", "text/html");
    });
    server.serveStatic("/", LittleFS, "/");
  } else {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(500, "text/plain", "LittleFS is not mounted\n");
    });
  }

  server.onNotFound([](AsyncWebServerRequest *request) {
    server_response(request, 404);
  });

  server.begin();
}

void loop() {
  unsigned long nowMs = millis();
  wl_status_t wifiStatus = WiFi.status();

  if (wifiStatus != WL_CONNECTED && (nowMs - lastWifiReconnect) >= WIFI_RECONNECT_PERIOD_MS) {
    beginWifiConnection(nowMs);
    wifiStatus = WiFi.status();
  }

  if (wifiStatus == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      if (nowMs - lastMqttReconnect >= MQTT_RECONNECT_PERIOD_MS) {
        lastMqttReconnect = nowMs;
        connectMqtt();
      }
    }
  }

  if (mqttClient.connected()) {
    if (!mqttWasConnected) {
      mqttWasConnected = true;
      publishMqttStartMessages();
    }
    qingPingAdapter.tick(nowMs);
  } else {
    mqttWasConnected = false;
  }

  for (int i = 0; i < RELAY_NUM; i++) {
    channelControllers[i].handle(nowMs);
  }

  int p = nowMs % DS18B20_REQUEST_PERIOD;
  if (p == 0) {
    int n = (nowMs / DS18B20_REQUEST_PERIOD) % DS18B20_NUM_REQUESTS;
    if (n != prev_n) {
      prev_n = n;
      if (n == 0) {
        publishMqttAllSensorsState();
        DS18B20.requestTemperatures();
      } else {
        float t = DS18B20.getTempC(DS18B20_DEVICES[n - 1]);
        if (isValidT(t)) {
          DS18B20_values[n - 1].temperature = t;
          time_t ts = time(nullptr);
          if (ts <= 0) {
            ts = 1;
          }
          DS18B20_values[n - 1].timestamp = ts;
        }
      }
    }
  }

  updateStatusLeds(nowMs);
  ArduinoOTA.handle();
  yield();
}
