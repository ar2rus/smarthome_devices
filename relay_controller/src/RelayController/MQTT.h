#ifndef MQTT_HELPERS_H
#define MQTT_HELPERS_H

#include <AsyncMqttClient.h>

#include "RelayController.h"
#include "Relay.h"

extern AsyncMqttClient mqttClient;
extern bool mqttWasConnected;

bool hasConfiguredMqttSettings();
void setupMqttClient();
void configureMqttClient();
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
void publishMqttButtonEvent(const char* eventType);
void publishMqttSensorState(int sensorIndex);
void publishMqttOneWireState(bool enabled, bool includeScanDetails = false);
void publishMqttRelayState(int relayIndex, const RelayState& state);
void publishMqttThermostatState(int channelIndex, const ThermostatState& state);
String formatDeviceId(const uint8_t* address);

#endif
