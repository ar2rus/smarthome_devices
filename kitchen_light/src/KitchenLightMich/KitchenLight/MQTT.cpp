#include "MQTT.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

#include "Config.h"
#include "KitchenLight.h"

extern int light_state;
extern int dimmer_value;
extern bool switch_on(bool send_response);
extern bool switch_off(bool send_response);
extern bool switch_toggle(bool send_response);
extern bool dimmer_exec(int value, bool send_response);
extern int currentLightBrightness();
extern bool isPwmActive();

AsyncMqttClient mqttClient;
bool mqttWasConnected = false;
static char mqttIncomingPayloadBuffer[256];

bool hasConfiguredMqttSettings() {
  return persistedSettings.magic == SETTINGS_STORAGE_MAGIC &&
         persistedSettings.version == SETTINGS_STORAGE_VERSION &&
         persistedSettings.mqtt.host[0] != '\0' &&
         persistedSettings.mqtt.port > 0;
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

static String mqttLightMetaPayload() {
  String payload = "{";
  payload += "\"location\":\"kitchen\",";
  payload += "\"supportsBrightness\":true,";
  payload += "\"brightnessRange\":{\"min\":0,\"max\":";
  payload += String(PWM_RANGE);
  payload += "}";
  payload += "}";
  return payload;
}

static String mqttLightStatePayload() {
  const bool isOn = light_state == HIGH;
  const int brightness = currentLightBrightness();

  String payload = "{";
  payload += "\"state\":\"";
  payload += isOn ? "ON" : "OFF";
  payload += "\",";
  payload += "\"on\":";
  payload += isOn ? "true" : "false";
  payload += ",\"brightness\":";
  payload += String(brightness);
  payload += ",\"pwm\":";
  payload += isPwmActive() ? "true" : "false";
  payload += "}";
  return payload;
}

static String mqttHomeAssistantDiscoveryPayload() {
  String payload = "{";
  payload += "\"name\":\"Kitchen Light\",";
  payload += "\"unique_id\":\"" MQTT_CLIENT_ID "_light\",";
  payload += "\"object_id\":\"" MQTT_CLIENT_ID "_light\",";
  payload += "\"schema\":\"json\",";
  payload += "\"state_topic\":\"" MQTT_TOPIC_LIGHT_STATE "\",";
  payload += "\"command_topic\":\"" MQTT_TOPIC_LIGHT_SET "\",";
  payload += "\"brightness\":true,";
  payload += "\"brightness_scale\":";
  payload += String(PWM_RANGE);
  payload += ",\"supported_color_modes\":[\"brightness\"],";
  payload += "\"transition\":true,";
  payload += "\"availability_topic\":\"" MQTT_TOPIC_STATUS "\",";
  payload += "\"payload_available\":\"online\",";
  payload += "\"payload_not_available\":\"offline\",";
  payload += "\"device\":{\"identifiers\":[\"" MQTT_CLIENT_ID "\"],";
  payload += "\"name\":\"Kitchen Light\",";
  payload += "\"manufacturer\":\"KitchenLight\",";
  payload += "\"model\":\"" CLUNET_DEVICE_NAME "\"}";
  payload += "}";
  return payload;
}

static void publishMqttLightMeta() {
  if (!mqttClient.connected()) {
    return;
  }

  String payload = mqttLightMetaPayload();
  mqttClient.publish(MQTT_TOPIC_LIGHT_META, 0, true, payload.c_str());
}

static void publishHomeAssistantDiscovery() {
  if (!mqttClient.connected()) {
    return;
  }

  String payload = mqttHomeAssistantDiscoveryPayload();
  mqttClient.publish(MQTT_TOPIC_HOME_ASSISTANT_DISCOVERY, 1, true, payload.c_str());
}

void publishMqttLightState() {
  if (!mqttClient.connected()) {
    return;
  }

  String payload = mqttLightStatePayload();
  mqttClient.publish(MQTT_TOPIC_LIGHT_STATE, 0, true, payload.c_str());
}

void publishMqttButtonEvent(const char* eventType) {
  if (!mqttClient.connected()) {
    return;
  }

  mqttClient.publish(MQTT_TOPIC_BUTTON_EVENT, 0, false, eventType);
}

static bool parseBrightnessPayload(const uint8_t* payload, unsigned int length, int* brightness) {
  if (brightness == nullptr || payload == nullptr || length == 0) {
    return false;
  }

  String raw;
  raw.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    raw += static_cast<char>(payload[i]);
  }
  raw.trim();

  bool digitsOnly = raw.length() > 0;
  for (size_t i = 0; i < raw.length(); i++) {
    if (!isDigit(raw[i])) {
      digitsOnly = false;
      break;
    }
  }

  if (digitsOnly) {
    int value = raw.toInt();
    if (value < 0 || value > PWM_RANGE) {
      return false;
    }
    *brightness = value;
    return true;
  }

  DynamicJsonDocument doc(128);
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error || !doc.containsKey("brightness")) {
    return false;
  }

  int value = doc["brightness"].as<int>();
  if (value < 0 || value > PWM_RANGE) {
    return false;
  }

  *brightness = value;
  return true;
}

static bool parseBrightnessCommandPayload(
  const uint8_t* payload,
  unsigned int length,
  int* brightness,
  unsigned long* durationSeconds,
  String* effect,
  unsigned long* effectDurationMs
) {
  if (brightness == nullptr || durationSeconds == nullptr || effect == nullptr || effectDurationMs == nullptr) {
    return false;
  }

  *durationSeconds = 0;
  *effectDurationMs = 0;
  *effect = "";

  if (!parseBrightnessPayload(payload, length, brightness)) {
    DynamicJsonDocument doc(192);
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error || !doc.containsKey("brightness")) {
      return false;
    }

    int value = doc["brightness"].as<int>();
    if (value < 0 || value > PWM_RANGE) {
      return false;
    }
    *brightness = value;
  }

  DynamicJsonDocument doc(192);
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    return true;
  }

  if (doc.containsKey("duration")) {
    long durationValue = doc["duration"].as<long>();
    if (durationValue > 0) {
      *durationSeconds = (unsigned long)durationValue;
    }
  }

  if (doc.containsKey("effect")) {
    *effect = doc["effect"].as<String>();
    effect->trim();
  }

  if (doc.containsKey("effect_duration")) {
    long effectDurationValue = doc["effect_duration"].as<long>();
    if (effectDurationValue > 0) {
      *effectDurationMs = (unsigned long)effectDurationValue;
    }
  }

  return true;
}

static void subscribeMqttCommandTopics() {
  mqttClient.subscribe(MQTT_TOPIC_LIGHT_SET, 1);
  mqttClient.subscribe(MQTT_TOPIC_LIGHT_SET_ON, 1);
  mqttClient.subscribe(MQTT_TOPIC_LIGHT_SET_OFF, 1);
  mqttClient.subscribe(MQTT_TOPIC_LIGHT_SET_TOGGLE, 1);
  mqttClient.subscribe(MQTT_TOPIC_LIGHT_SET_BRIGHTNESS, 1);
}

static void mqttHomeAssistantCommandReceived(const uint8_t* payload, unsigned int length) {
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    return;
  }

  String state = doc["state"] | "";
  state.trim();
  state.toUpperCase();

  const bool hasBrightness = doc.containsKey("brightness");
  int brightness = hasBrightness ? doc["brightness"].as<int>() : 0;
  if (hasBrightness && (brightness < 0 || brightness > PWM_RANGE)) {
    return;
  }

  unsigned long durationSeconds = 0;
  if (doc.containsKey("duration")) {
    long value = doc["duration"].as<long>();
    if (value > 0) {
      durationSeconds = static_cast<unsigned long>(value);
    }
  }

  String effect = doc["effect"] | "";
  effect.trim();

  unsigned long effectDurationMs = 0;
  if (doc.containsKey("effect_duration")) {
    long value = doc["effect_duration"].as<long>();
    if (value > 0) {
      effectDurationMs = static_cast<unsigned long>(value);
    }
  }

  if (doc.containsKey("transition")) {
    float transitionSeconds = doc["transition"].as<float>();
    if (transitionSeconds > 0 && effectDurationMs == 0) {
      effectDurationMs = static_cast<unsigned long>(transitionSeconds * 1000.0f);
      if (effect.length() == 0) {
        effect = "fade";
      }
    }
  }

  if (state == "OFF") {
    if (effectDurationMs > 0) {
      applyMqttBrightnessCommand(0, 0, effect.c_str(), effectDurationMs);
    } else {
      switch_off(false);
    }
    return;
  }

  if (state == "TOGGLE") {
    switch_toggle(false);
    return;
  }

  if (hasBrightness) {
    applyMqttBrightnessCommand(brightness, durationSeconds, effect.c_str(), effectDurationMs);
    return;
  }

  if (state == "ON") {
    switch_on(false);
  }
}

static void mqttMessageReceived(char* topic, uint8_t* payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC_LIGHT_SET) == 0) {
    mqttHomeAssistantCommandReceived(payload, length);
    return;
  }

  if (strcmp(topic, MQTT_TOPIC_LIGHT_SET_ON) == 0) {
    switch_on(false);
    return;
  }

  if (strcmp(topic, MQTT_TOPIC_LIGHT_SET_OFF) == 0) {
    switch_off(false);
    return;
  }

  if (strcmp(topic, MQTT_TOPIC_LIGHT_SET_TOGGLE) == 0) {
    switch_toggle(false);
    return;
  }

  if (strcmp(topic, MQTT_TOPIC_LIGHT_SET_BRIGHTNESS) == 0) {
    int brightness = 0;
    unsigned long durationSeconds = 0;
    unsigned long effectDurationMs = 0;
    String effect = "";

    if (!parseBrightnessCommandPayload(payload, length, &brightness, &durationSeconds, &effect, &effectDurationMs)) {
      return;
    }

    applyMqttBrightnessCommand(brightness, durationSeconds, effect.c_str(), effectDurationMs);
  }
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
  subscribeMqttCommandTopics();
  publishHomeAssistantDiscovery();
  publishMqttLightMeta();
  publishMqttLightState();
}
