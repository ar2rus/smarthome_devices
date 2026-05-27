#ifndef MQTT_HELPERS_H
#define MQTT_HELPERS_H

#include <AsyncMqttClient.h>

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
void publishMqttLightState();
void publishMqttButtonEvent(const char* eventType);

#endif
