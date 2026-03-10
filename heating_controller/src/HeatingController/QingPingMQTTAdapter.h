#ifndef QINGPING_MQTT_ADAPTER_H
#define QINGPING_MQTT_ADAPTER_H

#include <Arduino.h>
#include <AsyncMqttClient.h>

class QingPingMQTTAdapter {
public:
  static const char* const kUpTopic;
  static const char* const kSensorTopic;
  static const char* const kDownTopic;
  static const unsigned long kDownPublishPeriodMs;

  explicit QingPingMQTTAdapter(AsyncMqttClient& mqttClient);

  void onMqttConnected(unsigned long nowMs);
  void tick(unsigned long nowMs);
  bool handleMqttMessage(
    const char* topic,
    const uint8_t* payload,
    size_t length
  );

private:
  AsyncMqttClient* mqttClient_;
  unsigned long lastDownPublishAtMs_;

  void publishDownConfig(unsigned long nowMs);
};

#endif
