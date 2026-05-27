#include "Config.h"

#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>

#include "MQTT.h"

PersistedSettings persistedSettings = {};
WifiState wifiState = WIFI_STATE_IDLE;
unsigned long rebootScheduledAt = 0;
unsigned long lastMqttReconnect = 0;
bool littleFsAvailable = false;
char accessPointSsid[32] = {};

static DNSServer dnsServer;
static unsigned long wifiConnectStartedAt = 0;
static unsigned long apModeStartedAt = 0;

static void startAccessPoint();

static bool parseIPAddress(const char* value, IPAddress& address) {
  return value != nullptr && value[0] != '\0' && address.fromString(value);
}

void initializeConfig() {
  EEPROM.begin(EEPROM_STORAGE_SIZE);
  loadPersistedSettings();
  snprintf(accessPointSsid, sizeof(accessPointSsid), "%s-%06X", WIFI_AP_SSID_PREFIX, ESP.getChipId());
}

bool hasSavedWiFiSettings() {
  return persistedSettings.magic == SETTINGS_STORAGE_MAGIC &&
         persistedSettings.version == SETTINGS_STORAGE_VERSION &&
         persistedSettings.wifi.ssid[0] != '\0';
}

bool hasConfiguredWiFiSettings() {
  return hasSavedWiFiSettings();
}

bool loadPersistedSettings() {
  EEPROM.get(0, persistedSettings);

  if (persistedSettings.magic != SETTINGS_STORAGE_MAGIC ||
      persistedSettings.version != SETTINGS_STORAGE_VERSION) {
    memset(&persistedSettings, 0, sizeof(persistedSettings));
    return false;
  }

  persistedSettings.wifi.ssid[sizeof(persistedSettings.wifi.ssid) - 1] = '\0';
  persistedSettings.wifi.password[sizeof(persistedSettings.wifi.password) - 1] = '\0';
  persistedSettings.wifi.ip[sizeof(persistedSettings.wifi.ip) - 1] = '\0';
  persistedSettings.wifi.gateway[sizeof(persistedSettings.wifi.gateway) - 1] = '\0';
  persistedSettings.wifi.subnet[sizeof(persistedSettings.wifi.subnet) - 1] = '\0';
  persistedSettings.wifi.dns[sizeof(persistedSettings.wifi.dns) - 1] = '\0';
  persistedSettings.wifi.useStaticIp = persistedSettings.wifi.useStaticIp == 1 ? 1 : 0;

  persistedSettings.mqtt.host[sizeof(persistedSettings.mqtt.host) - 1] = '\0';
  persistedSettings.mqtt.user[sizeof(persistedSettings.mqtt.user) - 1] = '\0';
  persistedSettings.mqtt.password[sizeof(persistedSettings.mqtt.password) - 1] = '\0';

  if (persistedSettings.wifi.useStaticIp) {
    IPAddress ipAddress;
    IPAddress gatewayAddress;
    IPAddress subnetMask;
    IPAddress dnsAddress;

    if (!parseIPAddress(persistedSettings.wifi.ip, ipAddress) ||
        !parseIPAddress(persistedSettings.wifi.gateway, gatewayAddress) ||
        !parseIPAddress(persistedSettings.wifi.subnet, subnetMask) ||
        (persistedSettings.wifi.dns[0] != '\0' && !parseIPAddress(persistedSettings.wifi.dns, dnsAddress))) {
      persistedSettings.wifi.useStaticIp = 0;
      memset(persistedSettings.wifi.ip, 0, sizeof(persistedSettings.wifi.ip));
      memset(persistedSettings.wifi.gateway, 0, sizeof(persistedSettings.wifi.gateway));
      memset(persistedSettings.wifi.subnet, 0, sizeof(persistedSettings.wifi.subnet));
      memset(persistedSettings.wifi.dns, 0, sizeof(persistedSettings.wifi.dns));
    }
  }

  if (persistedSettings.mqtt.host[0] == '\0') {
    persistedSettings.mqtt.port = 0;
    memset(persistedSettings.mqtt.user, 0, sizeof(persistedSettings.mqtt.user));
    memset(persistedSettings.mqtt.password, 0, sizeof(persistedSettings.mqtt.password));
  }

  return true;
}

bool savePersistedSettings(
  const String& ssid,
  const String& password,
  bool useStaticIp,
  const String& ip,
  const String& gateway,
  const String& subnet,
  const String& dns,
  const String& mqttHost,
  uint16_t mqttPort,
  const String& mqttUser,
  const String& mqttPassword
) {
  if (ssid.length() >= sizeof(persistedSettings.wifi.ssid) ||
      password.length() >= sizeof(persistedSettings.wifi.password) ||
      ip.length() >= sizeof(persistedSettings.wifi.ip) ||
      gateway.length() >= sizeof(persistedSettings.wifi.gateway) ||
      subnet.length() >= sizeof(persistedSettings.wifi.subnet) ||
      dns.length() >= sizeof(persistedSettings.wifi.dns) ||
      mqttHost.length() >= sizeof(persistedSettings.mqtt.host) ||
      mqttUser.length() >= sizeof(persistedSettings.mqtt.user) ||
      mqttPassword.length() >= sizeof(persistedSettings.mqtt.password)) {
    return false;
  }

  if (useStaticIp && ssid.length() > 0) {
    IPAddress ipAddress;
    IPAddress gatewayAddress;
    IPAddress subnetMask;
    IPAddress dnsAddress;

    if (!ipAddress.fromString(ip) ||
        !gatewayAddress.fromString(gateway) ||
        !subnetMask.fromString(subnet) ||
        (dns.length() > 0 && !dnsAddress.fromString(dns))) {
      return false;
    }
  }

  if (mqttHost.length() == 0) {
    mqttPort = 0;
  } else if (mqttPort == 0) {
    return false;
  }

  if (mqttPassword.length() > 0 && mqttUser.length() == 0) {
    return false;
  }

  memset(&persistedSettings, 0, sizeof(persistedSettings));
  persistedSettings.magic = SETTINGS_STORAGE_MAGIC;
  persistedSettings.version = SETTINGS_STORAGE_VERSION;

  ssid.toCharArray(persistedSettings.wifi.ssid, sizeof(persistedSettings.wifi.ssid));
  password.toCharArray(persistedSettings.wifi.password, sizeof(persistedSettings.wifi.password));
  persistedSettings.wifi.useStaticIp = (ssid.length() > 0 && useStaticIp) ? 1 : 0;
  ip.toCharArray(persistedSettings.wifi.ip, sizeof(persistedSettings.wifi.ip));
  gateway.toCharArray(persistedSettings.wifi.gateway, sizeof(persistedSettings.wifi.gateway));
  subnet.toCharArray(persistedSettings.wifi.subnet, sizeof(persistedSettings.wifi.subnet));
  dns.toCharArray(persistedSettings.wifi.dns, sizeof(persistedSettings.wifi.dns));

  mqttHost.toCharArray(persistedSettings.mqtt.host, sizeof(persistedSettings.mqtt.host));
  persistedSettings.mqtt.port = mqttPort;
  mqttUser.toCharArray(persistedSettings.mqtt.user, sizeof(persistedSettings.mqtt.user));
  mqttPassword.toCharArray(persistedSettings.mqtt.password, sizeof(persistedSettings.mqtt.password));

  EEPROM.put(0, persistedSettings);
  return EEPROM.commit();
}

static bool configureStationNetwork() {
  IPAddress noIp(0, 0, 0, 0);
  if (!hasSavedWiFiSettings() || !persistedSettings.wifi.useStaticIp) {
    return WiFi.config(noIp, noIp, noIp);
  }

  IPAddress ipAddress;
  IPAddress gatewayAddress;
  IPAddress subnetMask;
  IPAddress dnsAddress;

  if (!parseIPAddress(persistedSettings.wifi.ip, ipAddress) ||
      !parseIPAddress(persistedSettings.wifi.gateway, gatewayAddress) ||
      !parseIPAddress(persistedSettings.wifi.subnet, subnetMask)) {
    return false;
  }

  if (!parseIPAddress(persistedSettings.wifi.dns, dnsAddress)) {
    dnsAddress = gatewayAddress;
  }

  return WiFi.config(ipAddress, gatewayAddress, subnetMask, dnsAddress);
}

static void startStationConnection() {
  if (!hasConfiguredWiFiSettings()) {
    startAccessPoint();
    return;
  }

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);

  if (!configureStationNetwork()) {
    startAccessPoint();
    return;
  }

  const char* ssid = persistedSettings.wifi.ssid;
  const char* password = persistedSettings.wifi.password;

  if (password[0] != '\0') {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  wifiState = WIFI_STATE_CONNECTING;
  wifiConnectStartedAt = millis();
  apModeStartedAt = 0;
}

static void handleStationConnected() {
  if (wifiState == WIFI_STATE_STA_CONNECTED) {
    return;
  }

  wifiState = WIFI_STATE_STA_CONNECTED;
  lastMqttReconnect = 0;
  wifiConnectStartedAt = 0;
  apModeStartedAt = 0;

  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
}

static void startAccessPoint() {
  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }

  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(accessPointSsid, WIFI_AP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());
  wifiState = WIFI_STATE_AP_MODE;
  wifiConnectStartedAt = 0;
  apModeStartedAt = millis();
}

void updateWiFiState(unsigned long nowMs) {
  wl_status_t status = WiFi.status();

  switch (wifiState) {
    case WIFI_STATE_CONNECTING:
      if (status == WL_CONNECTED) {
        handleStationConnected();
      } else if (nowMs - wifiConnectStartedAt >= WIFI_CONNECT_TIMEOUT_MS) {
        startAccessPoint();
      }
      break;
    case WIFI_STATE_STA_CONNECTED:
      if (status != WL_CONNECTED) {
        startStationConnection();
      }
      break;
    case WIFI_STATE_AP_MODE:
      dnsServer.processNextRequest();
      if (hasConfiguredWiFiSettings() &&
          rebootScheduledAt == 0 &&
          (nowMs - apModeStartedAt) >= WIFI_AP_RESTART_TIMEOUT_MS) {
        scheduleReboot();
      }
      break;
    case WIFI_STATE_IDLE:
    default:
      if (hasConfiguredWiFiSettings()) {
        startStationConnection();
      } else {
        startAccessPoint();
      }
      break;
  }
}

void scheduleReboot() {
  rebootScheduledAt = millis() + CONFIG_REBOOT_DELAY_MS;
}

const char* wifiStateLabel() {
  switch (wifiState) {
    case WIFI_STATE_CONNECTING:
      return "connecting";
    case WIFI_STATE_STA_CONNECTED:
      return "connected";
    case WIFI_STATE_AP_MODE:
      return "access_point";
    case WIFI_STATE_IDLE:
    default:
      return "idle";
  }
}

String wifiModeId() {
  switch (wifiState) {
    case WIFI_STATE_CONNECTING:
      return "sta_connecting";
    case WIFI_STATE_STA_CONNECTED:
      return "sta_connected";
    case WIFI_STATE_AP_MODE:
      return "ap";
    case WIFI_STATE_IDLE:
    default:
      return "idle";
  }
}

void appendConfigPayload(DynamicJsonDocument& doc) {
  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = hasSavedWiFiSettings() ? persistedSettings.wifi.ssid : "";
  wifi["password"] = hasSavedWiFiSettings() ? persistedSettings.wifi.password : "";
  wifi["useStaticIp"] = hasSavedWiFiSettings() && persistedSettings.wifi.useStaticIp == 1;
  wifi["ip"] = hasSavedWiFiSettings() ? persistedSettings.wifi.ip : "";
  wifi["gateway"] = hasSavedWiFiSettings() ? persistedSettings.wifi.gateway : "";
  wifi["subnet"] = hasSavedWiFiSettings() ? persistedSettings.wifi.subnet : "";
  wifi["dns"] = hasSavedWiFiSettings() ? persistedSettings.wifi.dns : "";

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = persistedSettings.mqtt.host;
  mqtt["port"] = persistedSettings.mqtt.port > 0 ? persistedSettings.mqtt.port : 1883;
  mqtt["user"] = persistedSettings.mqtt.user;
  mqtt["password"] = persistedSettings.mqtt.password;
  mqtt["enabled"] = hasConfiguredMqttSettings();
  mqtt["connected"] = mqttClient.connected();

  JsonObject network = doc.createNestedObject("network");
  network["mode"] = wifiModeId();
  network["stateLabel"] = wifiStateLabel();
  network["apSsid"] = accessPointSsid;
  network["apPassword"] = WIFI_AP_PASSWORD;
  network["apIp"] = WiFi.softAPIP().toString();
  network["stationIp"] = WiFi.localIP().toString();
  network["savedWifi"] = hasSavedWiFiSettings();
  network["usingFallbackDefaults"] = false;
}

static bool copyBodyToRequestBuffer(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (total > 0 && index == 0) {
    request->_tempObject = malloc(total + 1);
    if (request->_tempObject == NULL) {
      request->send(500, "application/json", "{\"success\":false,\"message\":\"Недостаточно памяти\"}");
      return false;
    }
  }

  if (request->_tempObject == NULL) {
    return false;
  }

  memcpy((uint8_t*)request->_tempObject + index, data, len);
  if (index + len != total) {
    return false;
  }

  ((uint8_t*)request->_tempObject)[total] = '\0';
  return true;
}

void setupConfigApiRoutes(AsyncWebServer& server) {
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    appendConfigPayload(doc);

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  AsyncCallbackWebHandler* configHandler = new AsyncCallbackWebHandler();
  configHandler->setUri("/api/config");
  configHandler->setMethod(HTTP_POST);
  configHandler->onRequest([](AsyncWebServerRequest *request) {});
  configHandler->onBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!copyBodyToRequestBuffer(request, data, len, index, total)) {
      return;
    }

    String jsonStr = String((char*)request->_tempObject);
    free(request->_tempObject);
    request->_tempObject = NULL;

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
      request->send(400, "application/json", "{\"success\":false,\"message\":\"Ошибка разбора JSON\"}");
      return;
    }

    if (!doc["wifi"].is<JsonObject>() || !doc["mqtt"].is<JsonObject>()) {
      request->send(400, "application/json", "{\"success\":false,\"message\":\"Ожидаются секции wifi и mqtt\"}");
      return;
    }

    JsonObject wifi = doc["wifi"].as<JsonObject>();
    JsonObject mqtt = doc["mqtt"].as<JsonObject>();

    String ssid = wifi["ssid"] | "";
    String password = wifi["password"] | "";
    bool useStaticIp = wifi["useStaticIp"] | false;
    String ip = wifi["ip"] | "";
    String gateway = wifi["gateway"] | "";
    String subnet = wifi["subnet"] | "";
    String dns = wifi["dns"] | "";
    String mqttHost = mqtt["host"] | "";
    uint16_t mqttPort = mqtt["port"] | 1883;
    String mqttUser = mqtt["user"] | "";
    String mqttPassword = mqtt["password"] | "";

    ssid.trim();
    password.trim();
    ip.trim();
    gateway.trim();
    subnet.trim();
    dns.trim();
    mqttHost.trim();
    mqttUser.trim();
    mqttPassword.trim();

    if (!savePersistedSettings(
          ssid,
          password,
          useStaticIp,
          ip,
          gateway,
          subnet,
          dns,
          mqttHost,
          mqttPort,
          mqttUser,
          mqttPassword
        )) {
      request->send(400, "application/json", "{\"success\":false,\"message\":\"Не удалось сохранить настройки\"}");
      return;
    }

    scheduleReboot();

    DynamicJsonDocument responseDoc(1152);
    responseDoc["success"] = true;
    responseDoc["message"] = "Настройки сохранены, устройство перезагрузится";
    responseDoc["rebootScheduled"] = true;
    appendConfigPayload(responseDoc);

    String response;
    serializeJson(responseDoc, response);
    request->send(200, "application/json", response);
  });
  server.addHandler(configHandler);
}
