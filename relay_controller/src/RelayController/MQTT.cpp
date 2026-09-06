#include "MQTT.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "Config.h"
#include "OneWireWatchdog.h"
#include "Relay.h"
#include "Thermostat.h"

extern Relay* relays[RELAY_CHANNELS_NUM];
extern Thermostat* thermostats[THERMOSTAT_CHANNELS_NUM];
extern DS18B20Reading DS18B20_values[ONE_WIRE_NUM_DEVICES];
extern OneWire oneWire;
extern DallasTemperature DS18B20;
extern OneWireWatchdog oneWireWatchdog;
extern bool oneWirePowerEnabled;
extern bool ds18b20NeedsRequest;
extern bool oneWireWatchdogRestartInProgress;
extern void applyOneWireEnable(bool enabled);

AsyncMqttClient mqttClient;
bool mqttWasConnected = false;
static char mqttIncomingPayloadBuffer[256];

static bool findKnownOneWireDevice(const uint8_t* address, int* knownIndex) {
  if (address == nullptr) {
    return false;
  }

  for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
    if (memcmp(address, DS18B20_DEVICES[i], 8) == 0) {
      if (knownIndex != nullptr) {
        *knownIndex = i;
      }
      return true;
    }
  }

  return false;
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

static void appendOneWireScanJson(JsonArray devices) {
  uint8_t address[8];
  oneWire.reset_search();
  while (oneWire.search(address)) {
    JsonObject device = devices.createNestedObject();
    int knownIndex = -1;
    bool known = findKnownOneWireDevice(address, &knownIndex);
    bool crcOk = OneWire::crc8(address, 7) == address[7];

    device["id"] = formatDeviceId(address);
    device["family"] = address[0];
    device["crcOk"] = crcOk;
    device["known"] = known;
    device["knownIndex"] = known ? knownIndex : -1;
    device["location"] = known ? DS18B20_DEVICES_LOCATIONS[knownIndex] : nullptr;
  }
  oneWire.reset_search();
}

bool hasConfiguredMqttSettings() {
  return persistedSettings.magic == SETTINGS_STORAGE_MAGIC
    && persistedSettings.mqtt.host[0] != '\0'
    && persistedSettings.mqtt.port > 0;
}

void configureMqttClient() {
  if (!hasConfiguredMqttSettings()) {
    return;
  }

  mqttClient.setServer(persistedSettings.mqtt.host, persistedSettings.mqtt.port);
  mqttClient.setClientId(MQTT_CLIENT_ID);
  mqttClient.setKeepAlive(30);
  mqttClient.setWill(MQTT_TOPIC_STATUS, 1, true, "offline");
  if (persistedSettings.mqtt.user[0] != '\0') {
    mqttClient.setCredentials(persistedSettings.mqtt.user, persistedSettings.mqtt.password);
  }
}

void setupMqttClient() {
  mqttClient.onMessage(onMqttMessage);
  configureMqttClient();
}

static String mqttSensorMetaPayload(int sensorIndex) {
  String payload = "{";
  payload += "\"type\":\"DS18B20\",";
  payload += "\"units\":{";
  payload += "\"temperature\":\"C\"";
  payload += "},";
  payload += "\"location\":\"" + String(DS18B20_DEVICES_LOCATIONS[sensorIndex]) + "\"";
  payload += "}";
  return payload;
}

static String mqttSensorStatePayload(int sensorIndex) {
  String payload = "{";
  payload += "\"temperature\":" + String(DS18B20_values[sensorIndex].temperature, 2) + ",";
  payload += "\"timestamp\":" + String(static_cast<unsigned long>(DS18B20_values[sensorIndex].timestamp));
  payload += "}";
  return payload;
}

static String mqttSensorTopicBase(int sensorIndex) {
  return String(MQTT_TOPIC_SENSOR) + "/" + formatDeviceId(DS18B20_DEVICES[sensorIndex]);
}

static String mqttSensorMetaTopic(int sensorIndex) {
  return mqttSensorTopicBase(sensorIndex) + "/meta";
}

static String mqttSensorStateTopic(int sensorIndex) {
  return mqttSensorTopicBase(sensorIndex) + "/state";
}

static String mqttRelayTopicBase(int relayIndex) {
  return String(MQTT_TOPIC_RELAY) + "/" + RELAY_CHANNELS_CONFIG[relayIndex].topicName;
}

static String mqttRelayStateTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/state";
}

static String mqttRelayMetaTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/meta";
}

static String mqttRelaySetOnTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/set/on";
}

static String mqttRelaySetOffTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/set/off";
}

static String mqttRelaySetToggleTopic(int relayIndex) {
  return mqttRelayTopicBase(relayIndex) + "/set/toggle";
}

static String mqttOneWireStatePayload(bool enabled, bool includeScanDetails) {
  DynamicJsonDocument doc(includeScanDetails ? 2048 : 64);
  doc["enabled"] = enabled;
  if (includeScanDetails) {
    JsonArray devices = doc.createNestedArray("devices");
    if (enabled) {
      appendOneWireScanJson(devices);
    }
    doc["count"] = devices.size();
  }

  String payload;
  serializeJson(doc, payload);
  return payload;
}

static String mqttRelayStatePayload(const RelayState& state) {
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

static String mqttRelayMetaPayload(int relayIndex) {
  String payload = "{";
  payload += "\"location\":\"" + String(RELAY_CHANNELS_CONFIG[relayIndex].location) + "\"";
  payload += "}";
  return payload;
}

static void publishMqttRelayMeta(int relayIndex) {
  if (!mqttClient.connected()) {
    return;
  }

  String topic = mqttRelayMetaTopic(relayIndex);
  String payload = mqttRelayMetaPayload(relayIndex);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

static void publishMqttAllRelaysMeta() {
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

static void publishMqttRelayState(int relayIndex) {
  if (relays[relayIndex] == nullptr) {
    return;
  }
  publishMqttRelayState(relayIndex, relays[relayIndex]->getState());
}

static void publishMqttAllRelaysState() {
  for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
    publishMqttRelayState(i);
  }
}

static String mqttThermostatTopicBase(int channelIndex) {
  return String(MQTT_TOPIC_THERMOSTAT) + "/" + THERMOSTAT_CHANNELS_CONFIG[channelIndex].topicName;
}

static String mqttThermostatStateTopic(int channelIndex) {
  return mqttThermostatTopicBase(channelIndex) + "/state";
}

static String mqttThermostatMetaTopic(int channelIndex) {
  return mqttThermostatTopicBase(channelIndex) + "/meta";
}

static String mqttThermostatMetaPayload(int channelIndex) {
  String payload = "{";
  payload += "\"location\":\"" + String(THERMOSTAT_CHANNELS_CONFIG[channelIndex].location) + "\"";
  payload += "}";
  return payload;
}

static String mqttThermostatStatePayload(const ThermostatState& state) {
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

static void publishMqttThermostatMeta(int channelIndex) {
  if (!mqttClient.connected()) {
    return;
  }

  String topic = mqttThermostatMetaTopic(channelIndex);
  String payload = mqttThermostatMetaPayload(channelIndex);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

static void publishMqttAllThermostatMeta() {
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

static void publishMqttAllThermostatState() {
  for (int i = 0; i < THERMOSTAT_CHANNELS_NUM; i++) {
    if (thermostats[i] == nullptr) {
      continue;
    }

    ThermostatState state;
    thermostats[i]->getState(&state);
    publishMqttThermostatState(i, state);
  }
}

static bool parseRelayDurationMinutes(const uint8_t* payload, unsigned int length, unsigned long& durationMinutes) {
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

static void subscribeMqttRelayCommandTopics() {
  for (int i = 0; i < RELAY_CHANNELS_NUM; i++) {
    String setOnTopic = mqttRelaySetOnTopic(i);
    String setOffTopic = mqttRelaySetOffTopic(i);
    String setToggleTopic = mqttRelaySetToggleTopic(i);
    mqttClient.subscribe(setOnTopic.c_str(), 1);
    mqttClient.subscribe(setOffTopic.c_str(), 1);
    mqttClient.subscribe(setToggleTopic.c_str(), 1);
  }
}

static void subscribeMqttOneWireCommandTopics() {
  mqttClient.subscribe(MQTT_TOPIC_ONEWIRE_SET_ON, 1);
  mqttClient.subscribe(MQTT_TOPIC_ONEWIRE_SET_OFF, 1);
  mqttClient.subscribe(MQTT_TOPIC_ONEWIRE_SCAN, 1);
}

static void mqttMessageReceived(char* topic, uint8_t* payload, unsigned int length) {
  (void)payload;
  (void)length;

  if (strcmp(topic, MQTT_TOPIC_ONEWIRE_SET_ON) == 0) {
    applyOneWireEnable(true);
    DS18B20.begin();
    DS18B20.setResolution(10);
    ds18b20NeedsRequest = true;
    oneWireWatchdogRestartInProgress = false;
    oneWireWatchdog.start(millis());
    publishMqttOneWireState(oneWirePowerEnabled);
    return;
  }

  if (strcmp(topic, MQTT_TOPIC_ONEWIRE_SET_OFF) == 0) {
    oneWireWatchdog.stop();
    oneWireWatchdogRestartInProgress = false;
    applyOneWireEnable(false);
    publishMqttOneWireState(oneWirePowerEnabled);
    return;
  }

  if (strcmp(topic, MQTT_TOPIC_ONEWIRE_SCAN) == 0) {
    publishMqttOneWireState(oneWirePowerEnabled, true);
    return;
  }

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

static void publishMqttSensorMeta(int sensorIndex) {
  if (!mqttClient.connected()) {
    return;
  }
  String topic = mqttSensorMetaTopic(sensorIndex);
  String payload = mqttSensorMetaPayload(sensorIndex);
  mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
}

static void publishMqttAllSensorsMeta() {
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

static void publishMqttAllSensorsState() {
  for (int i = 0; i < ONE_WIRE_NUM_DEVICES; i++) {
    publishMqttSensorState(i);
  }
}

void publishMqttOneWireState(bool enabled, bool includeScanDetails) {
  if (!mqttClient.connected()) {
    return;
  }

  String payload = mqttOneWireStatePayload(enabled, includeScanDetails);
  mqttClient.publish(MQTT_TOPIC_ONEWIRE_STATE, 0, true, payload.c_str());
}

void publishMqttButtonEvent(const char* eventType) {
  if (!mqttClient.connected()) {
    return;
  }

  mqttClient.publish(MQTT_TOPIC_BUTTON_EVENT, 0, false, eventType);
}

void connectMqtt() {
  if (wifiState != WIFI_STATE_STA_CONNECTED || WiFi.status() != WL_CONNECTED || !hasConfiguredMqttSettings()) {
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

  if (total >= sizeof(mqttIncomingPayloadBuffer) || index + len > total) {
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
  subscribeMqttOneWireCommandTopics();
  publishMqttAllSensorsMeta();
  publishMqttAllSensorsState();
  publishMqttOneWireState(oneWirePowerEnabled);
  publishMqttAllRelaysMeta();
  publishMqttAllRelaysState();
  publishMqttAllThermostatMeta();
  publishMqttAllThermostatState();
}
