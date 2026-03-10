#include "QingPingMQTTAdapter.h"

#include <ArduinoJson.h>
#include <cstring>

const char* const QingPingMQTTAdapter::kUpTopic = "home/qingping/582D3470A3D5/up";
const char* const QingPingMQTTAdapter::kSensorTopic = "home/qingping/sensor/582D3470A3D5/state";
const char* const QingPingMQTTAdapter::kDownTopic = "home/qingping/582D3470A3D5/down";
const unsigned long QingPingMQTTAdapter::kDownPublishPeriodMs = 60UL * 60UL * 1000UL;

namespace {
bool isType12(JsonVariantConst typeVar) {
  if (typeVar.isNull()) {
    return false;
  }

  const char* typeText = typeVar.as<const char*>();
  if (typeText != nullptr) {
    return strcmp(typeText, "12") == 0;
  }

  return typeVar.as<int>() == 12;
}
}  // namespace

QingPingMQTTAdapter::QingPingMQTTAdapter(AsyncMqttClient& mqttClient)
  : mqttClient_(&mqttClient), lastDownPublishAtMs_(0) {
}

void QingPingMQTTAdapter::publishDownConfig(unsigned long nowMs) {
  static const char kDownPayload[] = "{\"type\":\"12\",\"up_itvl\":\"15\",\"duration\":\"3600\"}";
  if (mqttClient_ == nullptr) {
    return;
  }
  mqttClient_->publish(kDownTopic, 1, false, kDownPayload);
  lastDownPublishAtMs_ = nowMs;
}

void QingPingMQTTAdapter::onMqttConnected(unsigned long nowMs) {
  if (mqttClient_ == nullptr) {
    return;
  }
  mqttClient_->subscribe(kUpTopic, 1);
  publishDownConfig(nowMs);
}

void QingPingMQTTAdapter::tick(unsigned long nowMs) {
  if (mqttClient_ == nullptr || !mqttClient_->connected()) {
    return;
  }

  if ((nowMs - lastDownPublishAtMs_) >= kDownPublishPeriodMs) {
    publishDownConfig(nowMs);
  }
}

bool QingPingMQTTAdapter::handleMqttMessage(
  const char* topic,
  const uint8_t* payload,
  size_t length
) {
  if (mqttClient_ == nullptr) {
    return false;
  }

  if (topic == nullptr || strcmp(topic, kUpTopic) != 0) {
    return false;
  }

  if (payload == nullptr || length == 0) {
    return true;
  }

  DynamicJsonDocument inputDoc(3072);
  DeserializationError error = deserializeJson(inputDoc, payload, length);
  if (error) {
    return true;
  }

  if (!isType12(inputDoc["type"])) {
    return true;
  }

  JsonArrayConst sensorData = inputDoc["sensorData"].as<JsonArrayConst>();
  if (sensorData.isNull() || sensorData.size() == 0) {
    return true;
  }

  JsonObjectConst first = sensorData[0].as<JsonObjectConst>();
  if (first.isNull()) {
    return true;
  }

  DynamicJsonDocument outputDoc(768);
  JsonObject output = outputDoc.to<JsonObject>();

  JsonVariantConst timestampField = first["timestamp"];
  if (!timestampField.isNull()) {
    if (timestampField.is<JsonObjectConst>()) {
      JsonVariantConst timestampValue = timestampField["value"];
      if (!timestampValue.isNull()) {
        output["timestamp"] = timestampValue;
      }
    } else {
      output["timestamp"] = timestampField;
    }
  }

  for (JsonPairConst pair : first) {
    const char* name = pair.key().c_str();
    if (strcmp(name, "timestamp") == 0) {
      continue;
    }

    JsonVariantConst sensorField = pair.value();
    JsonObjectConst sensorObject = sensorField.as<JsonObjectConst>();
    if (sensorObject.isNull()) {
      continue;
    }

    int status = sensorObject["status"] | -1;
    if (status != 0) {
      continue;
    }

    JsonVariantConst value = sensorObject["value"];
    if (value.isNull()) {
      continue;
    }

    output[name] = value;
  }

  if (output.size() == 0) {
    return true;
  }

  String normalizedPayload;
  serializeJson(outputDoc, normalizedPayload);
  mqttClient_->publish(kSensorTopic, 1, true, normalizedPayload.c_str());
  return true;
}
