/**
    Use 3.1.2 esp8266 core
    lwip v2 Higher bandwidth; CPU 80 MHz
    1M (FS: 128K)

    dependencies:
    https://github.com/me-no-dev/ESPAsyncWebServer
    https://github.com/ar2rus/ClunetMulticast

 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <EEPROM.h>
#include <TZ.h>

#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include "Config.h"
#include "KitchenLight.h"
#include "MQTT.h"

AsyncWebServer server(80);
ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);

const int BUTTON_BRIGHTNESS_DEFAULT = PWM_RANGE;
const int BUTTON_BRIGHTNESS_MIN = 1;

int button_state = HIGH;
int button_raw_state = HIGH;
int light_state = LOW;

int dimmer_value = 0;
int button_brightness_value = BUTTON_BRIGHTNESS_DEFAULT;
int button_dimming_direction = -1;

//fade-in
unsigned long fade_in_start_time = 0;
unsigned long button_state_changed_time = 0;
unsigned long button_pressed_time = 0;
unsigned long button_dimming_start_time = 0;
unsigned long last_button_on_time = 0;
unsigned long mqtt_light_off_deadline = 0;
unsigned long mqtt_effect_started_at = 0;
unsigned long mqtt_effect_duration_ms = 0;
unsigned long mqtt_light_off_effect_duration_ms = 0;

bool button_press_started_while_on = false;
bool button_dimming_active = false;
bool button_cycle_pending = false;
bool mqtt_effect_active = false;
bool mqtt_light_off_use_effect = false;
int button_dimming_start_value = BUTTON_BRIGHTNESS_DEFAULT;
int mqtt_effect_start_brightness = 0;
int mqtt_effect_target_brightness = 0;
byte button_on_cycle_count = 0;
bool clunet_handlers_initialized = false;
uint32_t clunetDiscoveryRequestsSeen = 0;
uint32_t clunetDiscoveryResponsesSent = 0;
uint32_t clunetDiscoveryLastSource = 0;
unsigned long clunetDiscoveryLastSeenAt = 0;
unsigned long clunetDiscoveryLastResponseSentAt = 0;

int clunetDimmerToPwm(int value) {
  return (value * PWM_RANGE + CLUNET_DIMMER_RANGE / 2) / CLUNET_DIMMER_RANGE;
}

int pwmToClunetDimmer(int value) {
  return (value * CLUNET_DIMMER_RANGE + PWM_RANGE / 2) / PWM_RANGE;
}

void initializeClunetHandlers() {
  if (clunet_handlers_initialized) {
    return;
  }

  clunet.onPacketSniff([](clunet_packet* packet){
    if (packet->command == CLUNET_COMMAND_DISCOVERY) {
      clunetDiscoveryRequestsSeen++;
      clunetDiscoveryLastSource = packet->src;
      clunetDiscoveryLastSeenAt = millis();
    }
  });

  clunet.onPacketSent([](clunet_packet* packet){
    if (packet->command == CLUNET_COMMAND_DISCOVERY_RESPONSE) {
      clunetDiscoveryResponsesSent++;
      clunetDiscoveryLastResponseSentAt = millis();
    }
  });

  clunet.onPacketReceived([](clunet_packet* packet){
     switch (packet->command) {
        case CLUNET_COMMAND_SWITCH:
          if (packet->data[0] == 0xFF) { //info request
            if (packet->size == 1) {
              switchResponse(packet->src);
            }
          } else {
            if (packet->size == 2) {
              switch (packet->data[0]) {
                case 0x00:
                case 0x01:
                case 0x02:
                  if (packet->data[1] == RELAY_0_ID) {
                    switch_exec(packet->data[0], false);
                  }
                  break;
                case 0x03:
                  switch_exec((packet->data[1] >> (RELAY_0_ID - 1)) & 0x01, false);
                  break;
              }
              switchResponse(packet->src);
            }
          }
          break;
        case CLUNET_COMMAND_BUTTON:
          if (packet->size == 0) {
            buttonResponse(packet->src);
          }
          break;
        case CLUNET_COMMAND_DIMMER:
          if (packet->size == 1 && packet->data[0] == 0xFF) {
            dimmerResponse(packet->src);
          } else if (packet->size == 2) {
            //у нас только один канал. Проверяем, что команда для него
            if ((packet->data[0] >> (RELAY_0_ID - 1)) & 0x01) {
              dimmer_exec(clunetDimmerToPwm(packet->data[1]), false);
              dimmerResponse(packet->src);
              publishMqttLightState();
            }
          }
      }
  });

  clunet_handlers_initialized = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  initializeConfig();

  pinMode(BUTTON_PIN, INPUT);

  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, !light_state);

  button_state = digitalRead(BUTTON_PIN);
  button_raw_state = button_state;
  button_state_changed_time = millis();

  analogWriteRange(PWM_RANGE);
  analogWriteFreq(PWM_FREQUENCY);

  loadButtonBrightness();

  littleFsAvailable = LittleFS.begin();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(MQTT_CLIENT_ID);

  ArduinoOTA.setHostname(MQTT_CLIENT_ID);
  
  ArduinoOTA.onStart([]() {
    Serial.println("ArduinoOTA start update");
  });

  updateWiFiState(millis());
  ArduinoOTA.begin();
  setupMqttClient();
  initializeClunetHandlers();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (wifiState == WIFI_STATE_AP_MODE) {
      if (littleFsAvailable) {
        request->redirect("/config");
      } else {
        request->send(503, "text/plain", "Config UI unavailable\n");
      }
      return;
    }

    if (!littleFsAvailable) {
      request->send(503, "text/plain", "UI unavailable\n");
      return;
    }
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/ui", HTTP_GET, [](AsyncWebServerRequest *request){
    request->redirect("/");
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
    int r = 404;
    if (request->args() == 0) {
      if (switch_toggle(true)) {
        r = 200;
      }
    }
    server_response(request, r);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!littleFsAvailable) {
      request->send(503, "text/plain", "Config UI unavailable\n");
      return;
    }
    request->send(LittleFS, "/config.html", "text/html");
  });

  server.on("/api/light/state", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    doc["on"] = light_state == HIGH;
    doc["brightness"] = currentLightBrightness();
    doc["pwm"] = isPwmActive();
    doc["buttonBrightness"] = button_brightness_value;
    doc["mqttConnected"] = mqttClient.connected();
    doc["wifiMode"] = wifiModeId();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/debug/clunet", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    doc["clunetConnected"] = clunet.connected();
    doc["discoveryRequestsSeen"] = clunetDiscoveryRequestsSeen;
    doc["discoveryResponsesSent"] = clunetDiscoveryResponsesSent;
    doc["lastDiscoverySource"] = clunetDiscoveryLastSource;
    doc["lastDiscoverySeenAt"] = clunetDiscoveryLastSeenAt;
    doc["lastDiscoveryResponseSentAt"] = clunetDiscoveryLastResponseSentAt;
    doc["udpPacketsSeen"] = clunet.udpPacketsSeen();
    doc["udpPacketsInvalidLength"] = clunet.udpPacketsInvalidLength();
    doc["udpPacketsInvalidSize"] = clunet.udpPacketsInvalidSize();
    doc["lastInvalidPacketLen"] = clunet.lastInvalidPacketLen();
    doc["lastInvalidPacketDeclaredSize"] = clunet.lastInvalidPacketDeclaredSize();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){  //on and dimmer
    int r = 404;
      switch (request->args()) {
        case 0:
          if (switch_on(true)){
            r = 200;
          }
          break;
        case 1:
          if(request->hasArg("d")){//dimmer: 0 - 1023
            String arg = request->arg("d");
            
            //check digits in arg value
            byte num_digits = 0;
            for (byte i = 0; i < arg.length(); i++) {
              if (isDigit(arg.charAt(i))) {
                num_digits++;
              }else{
                num_digits = 0;
                break;
              }
            }

            r = 400;
            if (num_digits && num_digits <= 4) {  //0-1023, maximum 4 digits
              if (dimmer_exec(arg.toInt(), true)) {
                r = 200;
              }
            }
          }
          break;
      }
    server_response(request, r);
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){  //off
    int r = 404;
    if (request->args() == 0) {
      if (switch_off(true)){
        r = 200;
      }
    }
    server_response(request, r);
  });

  server.on("/fadein", HTTP_GET, [](AsyncWebServerRequest *request){ //fade-in
    int r = 404;
    if (request->args() == 0) {
      if (fade_in_start()){
        r = 200;
      }
    }
    server_response(request, r);
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest * request) {
    scheduleReboot();
    request->send(200, "text/plain", "Reboot scheduled\n");
  });

  setupConfigApiRoutes(server);
  
  server.onNotFound( [](AsyncWebServerRequest *request) {
    if (wifiState == WIFI_STATE_AP_MODE) {
      request->redirect("/config");
      return;
    }
    server_response(request, 404);
  });

  server.begin();
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

int normalizeButtonBrightness(int value) {
  if (value < BUTTON_BRIGHTNESS_MIN || value > PWM_RANGE) {
    return BUTTON_BRIGHTNESS_DEFAULT;
  }
  return value;
}

int currentBrightnessForButton() {
  if (!light_state) {
    return 0;
  }

  if (dimmer_value > 0) {
    return dimmer_value;
  }

  return PWM_RANGE;
}

int currentLightBrightness() {
  return currentBrightnessForButton();
}

bool isPwmActive() {
  return light_state && dimmer_value > 0 && dimmer_value < PWM_RANGE;
}

void cancelMqttLightOffTimer() {
  mqtt_light_off_deadline = 0;
  mqtt_light_off_use_effect = false;
  mqtt_light_off_effect_duration_ms = 0;
}

void cancelMqttBrightnessEffect() {
  mqtt_effect_active = false;
  mqtt_effect_started_at = 0;
  mqtt_effect_duration_ms = 0;
}

void cancelMqttAutomation() {
  cancelMqttLightOffTimer();
  cancelMqttBrightnessEffect();
}

void scheduleMqttLightOffTimer(unsigned long durationSeconds, bool useEffect, unsigned long effectDurationMs) {
  if (durationSeconds == 0) {
    cancelMqttLightOffTimer();
    return;
  }

  mqtt_light_off_deadline = millis() + durationSeconds * 1000UL;
  mqtt_light_off_use_effect = useEffect;
  mqtt_light_off_effect_duration_ms = effectDurationMs > 0 ? effectDurationMs : MQTT_LIGHT_EFFECT_DURATION_DEFAULT_MS;
}

bool mqttEffectNameMatches(const char* effectName, const char* expected) {
  if (effectName == nullptr || expected == nullptr) {
    return false;
  }

  String actual = effectName;
  actual.trim();
  return actual.equalsIgnoreCase(expected);
}

void startMqttBrightnessEffect(int targetBrightness, unsigned long durationMs) {
  mqtt_effect_active = true;
  mqtt_effect_started_at = millis();
  mqtt_effect_duration_ms = durationMs > 0 ? durationMs : MQTT_LIGHT_EFFECT_DURATION_DEFAULT_MS;
  mqtt_effect_start_brightness = currentLightBrightness();
  mqtt_effect_target_brightness = targetBrightness;
}

void finishMqttBrightnessEffect() {
  if (!mqtt_effect_active) {
    return;
  }

  mqtt_effect_active = false;

  if (mqtt_effect_target_brightness >= PWM_RANGE) {
    switchExecute(0x01);
  } else {
    dimmerExecute(mqtt_effect_target_brightness);
  }

  publishMqttLightState();
}

void updateMqttBrightnessEffect(unsigned long now) {
  if (!mqtt_effect_active) {
    return;
  }

  unsigned long elapsed = now - mqtt_effect_started_at;
  if (elapsed >= mqtt_effect_duration_ms) {
    finishMqttBrightnessEffect();
    return;
  }

  long span = mqtt_effect_target_brightness - mqtt_effect_start_brightness;
  int value = mqtt_effect_start_brightness + (span * (long)elapsed) / (long)mqtt_effect_duration_ms;

  if (value < 0) {
    value = 0;
  } else if (value > PWM_RANGE) {
    value = PWM_RANGE;
  }

  dimmerExecute(value);
}

void updateMqttLightOffTimer(unsigned long now) {
  if (mqtt_light_off_deadline == 0) {
    return;
  }

  if ((long)(now - mqtt_light_off_deadline) >= 0) {
    mqtt_light_off_deadline = 0;

    if (mqtt_light_off_use_effect && currentLightBrightness() > 0) {
      unsigned long effectDurationMs = mqtt_light_off_effect_duration_ms;
      mqtt_light_off_use_effect = false;
      mqtt_light_off_effect_duration_ms = 0;
      startMqttBrightnessEffect(0, effectDurationMs);
      return;
    }

    cancelMqttAutomation();
    switch_exec(0x00, false);
  }
}

bool applyMqttBrightnessCommand(int brightness, unsigned long durationSeconds, const char* effectName, unsigned long effectDurationMs) {
  if (brightness < 0 || brightness > PWM_RANGE) {
    return false;
  }

  cancelMqttAutomation();

  bool useFadeEffect = mqttEffectNameMatches(effectName, "fade") || mqttEffectNameMatches(effectName, "rising");
  unsigned long actualEffectDurationMs = effectDurationMs > 0 ? effectDurationMs : MQTT_LIGHT_EFFECT_DURATION_DEFAULT_MS;

  if (brightness == 0) {
    if (useFadeEffect && currentLightBrightness() > 0) {
      startMqttBrightnessEffect(0, actualEffectDurationMs);
      return true;
    }
    return switch_exec(0x00, false);
  }

  if (durationSeconds > 0) {
    scheduleMqttLightOffTimer(durationSeconds, useFadeEffect, actualEffectDurationMs);
  }

  if (useFadeEffect) {
    startMqttBrightnessEffect(brightness, actualEffectDurationMs);
    return true;
  }

  bool r;
  if (brightness >= PWM_RANGE) {
    r = switch_exec(0x01, false);
  } else {
    r = dimmer_exec(brightness, false);
  }

  if (r) {
    publishMqttLightState();
  }

  return r;
}

void writeButtonBrightnessToEeprom(int value) {
  button_brightness_value = normalizeButtonBrightness(value);

  ButtonBrightnessSettings settings = {
    BUTTON_BRIGHTNESS_SETTINGS_MAGIC,
    (uint8_t)button_brightness_value
  };

  EEPROM.put(BUTTON_BRIGHTNESS_EEPROM_OFFSET, settings);
  EEPROM.commit();
}

void saveButtonBrightness(int value) {
  value = normalizeButtonBrightness(value);

  if (button_brightness_value != value) {
    writeButtonBrightnessToEeprom(value);
  }
}

void loadButtonBrightness() {
  ButtonBrightnessSettings settings;
  EEPROM.get(BUTTON_BRIGHTNESS_EEPROM_OFFSET, settings);

  if (settings.magic == BUTTON_BRIGHTNESS_SETTINGS_MAGIC &&
      settings.brightness >= BUTTON_BRIGHTNESS_MIN &&
      settings.brightness <= PWM_RANGE) {
    button_brightness_value = settings.brightness;
  } else {
    writeButtonBrightnessToEeprom(BUTTON_BRIGHTNESS_DEFAULT);
  }
}

bool shouldResetPwmOnButtonTurnOn(unsigned long now) {
  if (!button_cycle_pending) {
    button_on_cycle_count = 0;
    last_button_on_time = 0;
    return false;
  }

  button_cycle_pending = false;

  if (last_button_on_time && now - last_button_on_time <= BUTTON_PWM_RESET_WINDOW) {
    button_on_cycle_count++;
  } else {
    button_on_cycle_count = 1;
  }

  last_button_on_time = now;

  if (button_on_cycle_count >= 2) {
    button_on_cycle_count = 0;
    last_button_on_time = 0;
    return true;
  }

  return false;
}

bool buttonTurnOn(unsigned long now) {
  fade_in_stop(false);

  int brightness = button_brightness_value;

  if (shouldResetPwmOnButtonTurnOn(now)) {
    brightness = BUTTON_BRIGHTNESS_DEFAULT;
    writeButtonBrightnessToEeprom(brightness);
  }

  bool r;
  if (brightness >= PWM_RANGE) {
    r = switch_exec(0x01, false);
  } else {
    r = dimmer_exec(brightness, false);
  }

  if (r) {
    switchResponse(CLUNET_ADDRESS_BROADCAST);
    if (brightness < PWM_RANGE) {
      dimmerResponse(CLUNET_ADDRESS_BROADCAST);
    }
    publishMqttLightState();
  }

  return r;
}

bool buttonTurnOff() {
  int brightness = currentBrightnessForButton();
  if (brightness > 0) {
    saveButtonBrightness(brightness);
  }

  button_cycle_pending = true;
  return switch_exec(0x00, true);
}

void startButtonDimming(unsigned long now) {
  if (button_dimming_active || !button_press_started_while_on || !light_state) {
    return;
  }

  fade_in_stop(false);
  publishMqttButtonEvent("hold");

  button_dimming_active = true;
  button_dimming_start_time = now;
  button_dimming_start_value = currentBrightnessForButton();

  if (button_dimming_start_value >= PWM_RANGE) {
    button_dimming_direction = -1;
  } else if (button_dimming_start_value <= BUTTON_BRIGHTNESS_MIN) {
    button_dimming_direction = 1;
  }
}

void updateButtonDimming(unsigned long now) {
  if (!button_dimming_active) {
    return;
  }

  unsigned long elapsed = now - button_dimming_start_time;
  long delta = (long)elapsed * PWM_RANGE / BUTTON_PWM_TRAVEL_TIME;
  int value = button_dimming_start_value + button_dimming_direction * delta;

  if (value < BUTTON_BRIGHTNESS_MIN) {
    value = BUTTON_BRIGHTNESS_MIN;
  } else if (value > PWM_RANGE) {
    value = PWM_RANGE;
  }

  if (value != currentBrightnessForButton()) {
    dimmer_exec(value, false);
  }
}

void stopButtonDimming(bool send_response) {
  if (!button_dimming_active) {
    return;
  }

  button_dimming_active = false;

  int brightness = currentBrightnessForButton();
  saveButtonBrightness(brightness);

  if (brightness >= PWM_RANGE) {
    button_dimming_direction = -1;
  } else if (brightness <= BUTTON_BRIGHTNESS_MIN) {
    button_dimming_direction = 1;
  } else {
    button_dimming_direction = -button_dimming_direction;
  }

  if (send_response) {
    dimmerResponse(CLUNET_ADDRESS_BROADCAST);
  }
  publishMqttLightState();
}

void switchResponse(unsigned char address) {
  char info = (light_state << (RELAY_0_ID - 1));
  clunet.send(address, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

bool switchExecute(byte command) {
  switch (command) {
    case 0x00:  //откл
      light_state = LOW;
      break;
    case 0x01: //вкл
      light_state = HIGH;
      break;
    case 0x02: //перекл
      light_state = !light_state;
      break;
    default:
      return false;
  }

  //disable pwm
  dimmer_value = 0;
  analogWrite(LIGHT_PIN, dimmer_value);
  //set value
  digitalWrite(LIGHT_PIN, !light_state);
  return true;
}

bool switch_exec(byte command, bool send_response) {
  cancelMqttAutomation();
  bool r = switchExecute(command);
  if (r) {
    if (send_response) {
      switchResponse(CLUNET_ADDRESS_BROADCAST);
    }
    fade_in_stop(false);
    publishMqttLightState();
  }
  return r;
}

bool switch_on(bool send_response) {
  return switch_exec(0x01, send_response);
}

bool switch_off(bool send_response) {
  return switch_exec(0x00, send_response);
}

bool switch_toggle(bool send_response) {
  return switch_exec(0x02, send_response);
}

void dimmerResponse(unsigned char address) {
  char data[] = {1, RELAY_0_ID, (char)pwmToClunetDimmer(dimmer_value)};
  clunet.send(address, CLUNET_COMMAND_DIMMER_INFO, data, sizeof(data));
}

bool dimmerExecute(int value) {
  if (value >= 0 && value <= PWM_RANGE) {
    dimmer_value = value;
    light_state = value > 0;
    analogWrite(LIGHT_PIN, PWM_RANGE - dimmer_value);
    return true;
  }
  return false;
}

bool dimmer_exec(int value, bool send_response) {
  cancelMqttAutomation();
  bool r = dimmerExecute(value);
  if (r && send_response) {
    dimmerResponse(CLUNET_ADDRESS_BROADCAST);
    publishMqttLightState();
  }
  return r;
}

bool fade_in_start() {
  if (!fade_in_start_time) {
    fade_in_start_time = millis();
    return true;
  }
  return false;
}

bool fade_in_stop(bool send_response) {
  if (fade_in_start_time) {
    fade_in_start_time = 0;
    if (send_response) {
      dimmerResponse(CLUNET_ADDRESS_BROADCAST);
    }
    return true;
  }
  return false;
}

void buttonResponse(unsigned char address) {
  char data[] = {BUTTON_ID, !button_state};
  clunet.send(address, CLUNET_COMMAND_BUTTON_INFO, data, sizeof(data));
}

void loop() {
  int button_tmp = digitalRead(BUTTON_PIN);
  unsigned long m = millis();

  updateWiFiState(m);

  if (wifiState == WIFI_STATE_STA_CONNECTED &&
      WiFi.status() == WL_CONNECTED &&
      !mqttClient.connected() &&
      hasConfiguredMqttSettings() &&
      (m - lastMqttReconnect) >= MQTT_RECONNECT_PERIOD_MS) {
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

  if (rebootScheduledAt && (long)(m - rebootScheduledAt) >= 0) {
    ESP.restart();
  }

  if (wifiState == WIFI_STATE_STA_CONNECTED && WiFi.status() == WL_CONNECTED) {
    if (!clunet.connected()) {
      clunet.connect();
    }
  } else if (clunet.connected()) {
    clunet.close();
  }

  updateMqttBrightnessEffect(m);
  updateMqttLightOffTimer(m);

  if (button_tmp != button_raw_state) {
    button_raw_state = button_tmp;
    button_state_changed_time = m;
  }

  if (button_state != button_raw_state && m - button_state_changed_time >= DELAY_BEFORE_TOGGLE) {
    button_state = button_raw_state;
    buttonResponse(CLUNET_ADDRESS_BROADCAST);
    publishMqttButtonEvent(button_state == LOW ? "pressed" : "released");

    if (button_state == LOW) {
      button_pressed_time = m;
      button_press_started_while_on = light_state;
      button_dimming_active = false;

      if (!button_press_started_while_on) {
        buttonTurnOn(m);
      }
    } else {
      if (button_dimming_active) {
        stopButtonDimming(true);
      } else if (button_press_started_while_on &&
                 button_pressed_time &&
                 m - button_pressed_time >= DELAY_BEFORE_PWM) {
        startButtonDimming(button_pressed_time + DELAY_BEFORE_PWM);
        updateButtonDimming(m);
        stopButtonDimming(true);
      } else if (button_press_started_while_on &&
                 button_pressed_time &&
                 m - button_pressed_time < DELAY_BEFORE_PWM) {
        buttonTurnOff();
      }

      button_pressed_time = 0;
      button_press_started_while_on = false;
    }
  }

  if (button_state == LOW &&
      button_pressed_time &&
      button_press_started_while_on &&
      !button_dimming_active &&
      m - button_pressed_time >= DELAY_BEFORE_PWM) {
    startButtonDimming(m);
  }

  updateButtonDimming(m);

  //fade_in_update
  if (fade_in_start_time) {
    int v0 = (m - fade_in_start_time) % PWM_DOWN_UP_CYCLE_TIME;
    int v1 = v0 % PWM_DOWN_UP_CYCLE_TIME_2;
    if (v0 >= PWM_DOWN_UP_CYCLE_TIME_2) { //up
      dimmer_exec(PWM_RANGE * v1 / (PWM_DOWN_UP_CYCLE_TIME_2 - 1), false);
    } else { //down
      dimmer_exec(PWM_RANGE - PWM_RANGE * v1 / (PWM_DOWN_UP_CYCLE_TIME_2 - 1), false);
    }
  }

  ArduinoOTA.handle();
  yield();
}
