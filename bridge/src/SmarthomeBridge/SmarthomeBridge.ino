/**
 * Use 3.1.2 esp8266 core
 * lwip v2 Higher bandwidth; CPU 80 MHz
 * Flash size: 4M (FS: 1Mb / OTA: 1019 Kb) !!!
 * 
 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TZ.h>

#include <DNSServer.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include <LittleFS.h>

#include <ClunetMulticast.h>
#include <MessageDecoder.h>
#include <HexUtils.h>

#include "ClunetCommands.h"
#include "ClunetDevices.h"

#include "SmarthomeBridge.h"

#ifdef DEFAULT_MAX_SSE_CLIENTS
  #undef DEFAULT_MAX_SSE_CLIENTS 
  #define DEFAULT_MAX_SSE_CLIENTS 10
#endif

enum WifiState {
  WIFI_STATE_IDLE,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_STA_CONNECTED,
  WIFI_STATE_AP_MODE
};

struct WifiSettings {
  uint32_t magic;
  char ssid[33];
  char password[65];
  uint8_t use_static_ip;
  char ip[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
};

AsyncWebServer server(HTTP_PORT);
AsyncEventSource events("/events");
DNSServer dns_server;

ClunetMulticast clunet(CLUNET_ID, CLUNET_DEVICE);

WifiSettings wifi_settings = {};
WifiState wifi_state = WIFI_STATE_IDLE;

bool clunet_ready = false;
bool clunet_handlers_registered = false;
bool littlefs_ready = false;
bool ota_ready = false;

unsigned long wifi_connect_started_at = 0;
unsigned long clunet_connect_attempt_at = 0;
unsigned long reboot_scheduled_at = 0;

char access_point_ssid[32] = {};

long event_id = 0;
LinkedList<clunet_packet*> uartQueue = LinkedList<clunet_packet*>([](clunet_packet *m){ delete m; });
LinkedList<clunet_packet*> multicastQueue = LinkedList<clunet_packet*>([](clunet_packet *m){ delete m; });

LinkedList<ts_clunet_packet*> eventsQueue = LinkedList<ts_clunet_packet*>([](ts_clunet_packet *m){ delete m; });

LinkedList<api_request*> apiRequestsQueue = LinkedList<api_request*>([](api_request *r){ delete r; });
api_response* apiResponse = NULL;

#define UART_MESSAGE_CODE_CLUNET 1
#define UART_MESSAGE_CODE_FIRMWARE 2
#define UART_MESSAGE_CODE_DEBUG 10

const char UART_MESSAGE_PREAMBULE[] = {0xC9, 0xE7};

bool loadWiFiSettings();
bool saveWiFiSettings(
  const String& ssid,
  const String& password,
  bool use_static_ip,
  const String& ip,
  const String& gateway,
  const String& subnet,
  const String& dns
);
bool hasSavedWiFiSettings();
bool parseIPAddress(const char* value, IPAddress& address);
bool configureStationNetwork();
bool ensureClunetAvailable(AsyncWebServerRequest* request);

void setupWebServer();
void setupClunetCallbacks();
void updateWiFiState();
void startStationConnection();
void handleStationConnected();
void startAccessPoint();
void ensureClunetConnected();
void scheduleReboot();
void sendUiPage(AsyncWebServerRequest *request, const String& message = "", int status_code = 200);

String htmlEscape(const String& value);
String wifiStateLabel();
String networkHint();
String ipAddressValue(const char* value);

uint8_t uart_can_send(uint8_t length){
  return Serial.availableForWrite() >= length + 5;
}

uint8_t uart_can_send(clunet_packet* packet){
  return uart_can_send(packet->len());
}

uint8_t uart_send_message(char code, char* data, uint8_t length){
  if (!uart_can_send(length)){
    return 0;
  }
  
  uint8_t buf_length = length + 2;
  
  char buf[buf_length];
  buf[0] = length + 3;
  buf[1] = code;
  
  if (length){
    memcpy((void*)(buf + 2), data, length);
  }

  Serial.write((char*)UART_MESSAGE_PREAMBULE, 2);
  Serial.write(buf, buf_length);
  Serial.write(check_crc(buf, buf_length));

  return 1;
}

uint8_t uart_send_message(clunet_packet* packet){
  return uart_send_message(UART_MESSAGE_CODE_CLUNET, (char*)packet, packet->len());
}


void _request(AsyncWebServerRequest* webRequest, uint8_t address, uint8_t command, char* data, uint8_t size,
                int responseFilterCommand, long responseTimeout, bool _infoRequest, String _infoRequestId){
    if (!clunet_ready){
      webRequest->send(503, "text/plain", "Clunet is unavailable because WiFi is not connected\n");
      return;
    }

    webRequest->client()->setRxTimeout(5);
    api_request* ar = (api_request*)malloc(sizeof(api_request) + size);
    ar->webRequest = webRequest;
    ar->info = _infoRequest;
    if (_infoRequest){
      strcpy(ar->infoId, _infoRequestId.c_str());
    }
    ar->address = address;
    ar->command = command;
    ar->responseFilterCommand = responseFilterCommand;
    ar->responseTimeout = responseTimeout;
    ar->size = size;
    memcpy(ar->data, data, size);
    apiRequestsQueue.add(ar);

    ar->webRequest->onDisconnect([ar](){
      if (ar != NULL){
        ar->webRequest = NULL;
      }
    });
}

void _request(AsyncWebServerRequest* webRequest, uint8_t address, uint8_t command, char* data, uint8_t size,
                int responseFilterCommand, long responseTimeout){
    _request(webRequest, address, command, data, size, responseFilterCommand, responseTimeout, false, "");
}

int int_param(AsyncWebServerRequest* request, String param){
    return request->getParam(param)->value().toInt();
}

int address_param(AsyncWebServerRequest* request){
    return int_param(request, "a");
}

String id_param(AsyncWebServerRequest* request){
    return request->getParam("id")->value();
}

void api_200(AsyncWebServerRequest* request){
    request->send(200, "text/plain", "{\"body\": \"OK\"}");
}

void api_400(AsyncWebServerRequest* request, String params){
    request->send(400, "text/plain", request->url() + "?" + params);
}

void api_dimmer_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&id=channel_id&value=[0:100]");
}

void _api_dimmer(int address, int channel_id, int value){
    char data[] = {(char)channel_id, (char)map(value, 0, 100, 0, 255)};
    clunet.send(address, CLUNET_COMMAND_DIMMER, data, 2);
}

void api_dimmer(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("id") || !request->hasParam("value")){
        api_dimmer_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>100){
        api_dimmer_400(request);
        return;
    }

    _api_dimmer(address_param(request), id_param(request).toInt(), value);
    api_200(request); 
}

void api_switch_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&id=channel_id&value=[0:1]");
}

void _api_switch(int address, int channel_id, int value){
    char data[] = {(char)value, (char)channel_id};
    clunet.send(address, CLUNET_COMMAND_SWITCH, data, 2);
}

void api_switch(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("id") || !request->hasParam("value")){
        api_switch_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>1){
        api_switch_400(request);
        return;
    }

    _api_switch(address_param(request), id_param(request).toInt(), value);
    api_200(request);
}

void api_fan_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&value=[0:1]");
}

void _api_fan(int address, int value){
    char data = value ? 4 : 3;
    clunet.send(address, CLUNET_COMMAND_FAN, &data, 1);
}

void api_fan(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("value")){
        api_fan_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>1){
        api_fan_400(request);
        return;
    }

    _api_fan(address_param(request), value);
    api_200(request);
}

void api_door_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&value=[0:1]");
}

void _api_door(int address, int value){
    clunet.send(address, CLUNET_COMMAND_DOOR, (char*)&value, 1);
}

void api_door(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("value")){
        api_door_400(request);
        return;
    }

    int value = int_param(request, "value");
    if (value <0 || value>1){
        api_fan_400(request);
        return;
    }

    _api_door(address_param(request), value);
    api_200(request);
}

void info_switch_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address&id=channel_id");
}

void info_switch(AsyncWebServerRequest* request){
    if (!request->hasParam("a") || !request->hasParam("id")){
        info_switch_400(request);
        return;
    }

    char data = 0xFF;
    _request(request, address_param(request), CLUNET_COMMAND_SWITCH, &data, 1, CLUNET_COMMAND_SWITCH_INFO, 250, true, id_param(request));
}

void info_fan_400(AsyncWebServerRequest* request){
    api_400(request, "a=device_address");
}

void info_fan(AsyncWebServerRequest* request){
    if (!request->hasParam("a")){
        info_fan_400(request);
        return;
    }

    char data = 0xFF;
    _request(request, address_param(request), CLUNET_COMMAND_FAN, &data, 1, CLUNET_COMMAND_FAN_INFO, 250, true, "");
}

bool loadWiFiSettings() {
  EEPROM.get(0, wifi_settings);

  if (wifi_settings.magic != WIFI_SETTINGS_MAGIC) {
    memset(&wifi_settings, 0, sizeof(wifi_settings));
    return false;
  }

  wifi_settings.ssid[sizeof(wifi_settings.ssid) - 1] = '\0';
  wifi_settings.password[sizeof(wifi_settings.password) - 1] = '\0';
  wifi_settings.ip[sizeof(wifi_settings.ip) - 1] = '\0';
  wifi_settings.gateway[sizeof(wifi_settings.gateway) - 1] = '\0';
  wifi_settings.subnet[sizeof(wifi_settings.subnet) - 1] = '\0';
  wifi_settings.dns[sizeof(wifi_settings.dns) - 1] = '\0';
  wifi_settings.use_static_ip = wifi_settings.use_static_ip == 1 ? 1 : 0;

  if (wifi_settings.use_static_ip) {
    IPAddress ip;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns;

    if (
      !parseIPAddress(wifi_settings.ip, ip) ||
      !parseIPAddress(wifi_settings.gateway, gateway) ||
      !parseIPAddress(wifi_settings.subnet, subnet) ||
      (wifi_settings.dns[0] && !parseIPAddress(wifi_settings.dns, dns))
    ) {
      wifi_settings.use_static_ip = 0;
      memset(wifi_settings.ip, 0, sizeof(wifi_settings.ip));
      memset(wifi_settings.gateway, 0, sizeof(wifi_settings.gateway));
      memset(wifi_settings.subnet, 0, sizeof(wifi_settings.subnet));
      memset(wifi_settings.dns, 0, sizeof(wifi_settings.dns));
    }
  } else {
    memset(wifi_settings.ip, 0, sizeof(wifi_settings.ip));
    memset(wifi_settings.gateway, 0, sizeof(wifi_settings.gateway));
    memset(wifi_settings.subnet, 0, sizeof(wifi_settings.subnet));
    memset(wifi_settings.dns, 0, sizeof(wifi_settings.dns));
  }

  if (!wifi_settings.ssid[0]) {
    memset(&wifi_settings, 0, sizeof(wifi_settings));
    return false;
  }

  return true;
}

bool saveWiFiSettings(
  const String& ssid,
  const String& password,
  bool use_static_ip,
  const String& ip,
  const String& gateway,
  const String& subnet,
  const String& dns
) {
  if (!ssid.length() || ssid.length() >= sizeof(wifi_settings.ssid) || password.length() >= sizeof(wifi_settings.password)) {
    return false;
  }

  if (
    ip.length() >= sizeof(wifi_settings.ip) ||
    gateway.length() >= sizeof(wifi_settings.gateway) ||
    subnet.length() >= sizeof(wifi_settings.subnet) ||
    dns.length() >= sizeof(wifi_settings.dns)
  ) {
    return false;
  }

  if (use_static_ip) {
    IPAddress parsed_ip;
    IPAddress parsed_gateway;
    IPAddress parsed_subnet;
    IPAddress parsed_dns;

    if (!parsed_ip.fromString(ip) || !parsed_gateway.fromString(gateway) || !parsed_subnet.fromString(subnet)) {
      return false;
    }

    if (dns.length() && !parsed_dns.fromString(dns)) {
      return false;
    }
  }

  memset(&wifi_settings, 0, sizeof(wifi_settings));
  wifi_settings.magic = WIFI_SETTINGS_MAGIC;
  ssid.toCharArray(wifi_settings.ssid, sizeof(wifi_settings.ssid));
  password.toCharArray(wifi_settings.password, sizeof(wifi_settings.password));
  wifi_settings.use_static_ip = use_static_ip ? 1 : 0;
  ip.toCharArray(wifi_settings.ip, sizeof(wifi_settings.ip));
  gateway.toCharArray(wifi_settings.gateway, sizeof(wifi_settings.gateway));
  subnet.toCharArray(wifi_settings.subnet, sizeof(wifi_settings.subnet));
  dns.toCharArray(wifi_settings.dns, sizeof(wifi_settings.dns));

  EEPROM.put(0, wifi_settings);
  return EEPROM.commit();
}

bool hasSavedWiFiSettings() {
  return wifi_settings.magic == WIFI_SETTINGS_MAGIC && wifi_settings.ssid[0];
}

bool parseIPAddress(const char* value, IPAddress& address) {
  return value[0] && address.fromString(value);
}

bool configureStationNetwork() {
  IPAddress no_ip(0, 0, 0, 0);

  if (!wifi_settings.use_static_ip) {
    return WiFi.config(no_ip, no_ip, no_ip);
  }

  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;

  if (
    !parseIPAddress(wifi_settings.ip, ip) ||
    !parseIPAddress(wifi_settings.gateway, gateway) ||
    !parseIPAddress(wifi_settings.subnet, subnet)
  ) {
    return false;
  }

  if (!parseIPAddress(wifi_settings.dns, dns)) {
    dns = gateway;
  }

  return WiFi.config(ip, gateway, subnet, dns);
}

bool ensureClunetAvailable(AsyncWebServerRequest* request) {
  if (clunet_ready) {
    return true;
  }

  request->send(503, "text/plain", "Clunet is unavailable because WiFi is not connected\n");
  return false;
}

void setupClunetCallbacks() {
  if (clunet_handlers_registered) {
    return;
  }

  clunet.onPacketSniff([](clunet_packet* packet) {
    timeval tv;
    gettimeofday(&tv, nullptr);

    if (CLUNET_MULTICAST_DEVICE(packet->src)) {
      uartQueue.add(packet->copy());
    }

    ts_clunet_packet* tp = (ts_clunet_packet*)malloc(sizeof(ts_clunet_packet) + packet->len());
    packet->copy(&tp->packet);

    tp->timestamp_sec = (uint32_t)tv.tv_sec;
    tp->timestamp_ms = (uint16_t)(tv.tv_usec / 1000UL);

    eventsQueue.add(tp);
  });

  clunet.onResponseReceived([](int requestId, LinkedList<clunet_response*>* responses) {
    if (apiResponse != NULL) {
      if (apiResponse->webRequest != NULL) {
        DynamicJsonDocument doc(4196);
        JsonObject root = doc.to<JsonObject>();
        root["id"] = requestId;
        root["memory"] = ESP.getFreeHeap();
        JsonArray docArray;

        if (!apiResponse->info) {
          docArray = root.createNestedArray("responses");
        }

        for (auto i = responses->begin(); i != responses->end(); ++i) {
          clunet_response* response = *i;
          if (requestId == response->requestId) {
            if (apiResponse->info) {
              fillValueData(root, response->packet, apiResponse->infoId);
              break;
            } else {
              fillMessageJsonObject(docArray.createNestedObject(), 0, 0, response->packet);
            }
          }
        }

        String json;
        serializeJson(doc, json);
        apiResponse->webRequest->send(200, "application/json", json);
      }

      delete apiResponse;
      apiResponse = NULL;
    }
  });

  clunet_handlers_registered = true;
}

void startStationConnection() {
  if (!hasSavedWiFiSettings()) {
    startAccessPoint();
    return;
  }

  Serial1.print("Connecting to WiFi: ");
  Serial1.println(wifi_settings.ssid);

  dns_server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  if (!configureStationNetwork()) {
    Serial1.println("Invalid static IP settings, switching to AP mode");
    startAccessPoint();
    return;
  }

  if (wifi_settings.password[0]) {
    WiFi.begin(wifi_settings.ssid, wifi_settings.password);
  } else {
    WiFi.begin(wifi_settings.ssid);
  }

  wifi_state = WIFI_STATE_CONNECTING;
  wifi_connect_started_at = millis();
  ota_ready = false;
  clunet_ready = false;
}

void handleStationConnected() {
  if (wifi_state == WIFI_STATE_STA_CONNECTED) {
    return;
  }

  wifi_state = WIFI_STATE_STA_CONNECTED;
  wifi_connect_started_at = 0;

  Serial1.print("WiFi connected, IP: ");
  Serial1.println(WiFi.localIP());

  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  ArduinoOTA.begin();
  ota_ready = true;

  clunet_ready = false;
  clunet_connect_attempt_at = millis() - WIFI_CONNECT_RETRY_INTERVAL_MS;
  ensureClunetConnected();
}

void startAccessPoint() {
  if (wifi_state == WIFI_STATE_AP_MODE) {
    return;
  }

  Serial1.println("Starting AP mode");

  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(access_point_ssid, WIFI_AP_PASSWORD);
  dns_server.start(53, "*", WiFi.softAPIP());

  wifi_state = WIFI_STATE_AP_MODE;
  wifi_connect_started_at = 0;
  ota_ready = false;
  clunet_ready = false;

  Serial1.print("AP SSID: ");
  Serial1.println(access_point_ssid);
  Serial1.print("AP password: ");
  Serial1.println(WIFI_AP_PASSWORD);
  Serial1.print("AP IP: ");
  Serial1.println(WiFi.softAPIP());
}

void ensureClunetConnected() {
  if (wifi_state != WIFI_STATE_STA_CONNECTED || clunet_ready) {
    return;
  }

  unsigned long now = millis();
  if (clunet_connect_attempt_at && now - clunet_connect_attempt_at < WIFI_CONNECT_RETRY_INTERVAL_MS) {
    return;
  }

  clunet_connect_attempt_at = now;

  if (clunet.connect()) {
    setupClunetCallbacks();
    clunet_ready = true;
    Serial1.println("Clunet connected");
  }
}

void updateWiFiState() {
  wl_status_t status = WiFi.status();

  switch (wifi_state) {
    case WIFI_STATE_CONNECTING:
      if (status == WL_CONNECTED) {
        handleStationConnected();
      } else if (millis() - wifi_connect_started_at >= WIFI_CONNECT_TIMEOUT_MS) {
        Serial1.println("WiFi connection timeout, switching to AP mode");
        startAccessPoint();
      }
      break;
    case WIFI_STATE_STA_CONNECTED:
      if (status != WL_CONNECTED) {
        Serial1.println("WiFi lost, reconnecting");
        startStationConnection();
      } else {
        ensureClunetConnected();
      }
      break;
    case WIFI_STATE_AP_MODE:
      dns_server.processNextRequest();
      break;
    case WIFI_STATE_IDLE:
      if (hasSavedWiFiSettings()) {
        startStationConnection();
      } else {
        startAccessPoint();
      }
      break;
  }
}

void scheduleReboot() {
  reboot_scheduled_at = millis() + WIFI_REBOOT_DELAY_MS;
}

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 16);

  for (size_t i = 0; i < value.length(); i++) {
    switch (value.charAt(i)) {
      case '&':
        escaped += F("&amp;");
        break;
      case '<':
        escaped += F("&lt;");
        break;
      case '>':
        escaped += F("&gt;");
        break;
      case '"':
        escaped += F("&quot;");
        break;
      case '\'':
        escaped += F("&#39;");
        break;
      default:
        escaped += value.charAt(i);
        break;
    }
  }

  return escaped;
}

String wifiStateLabel() {
  switch (wifi_state) {
    case WIFI_STATE_CONNECTING:
      return F("Connecting to WiFi");
    case WIFI_STATE_STA_CONNECTED:
      return F("Connected to WiFi");
    case WIFI_STATE_AP_MODE:
      return F("Access Point mode");
    default:
      return F("Idle");
  }
}

String networkHint() {
  if (wifi_state == WIFI_STATE_STA_CONNECTED) {
    String hint = F("Connected as client. OTA and multicast bridge are active.<br>IP: ");
    hint += WiFi.localIP().toString();
    hint += F("<br>Saved SSID: ");
    hint += htmlEscape(String(wifi_settings.ssid));
    hint += F("<br>Addressing: ");
    hint += wifi_settings.use_static_ip ? F("Static IP") : F("DHCP");
    return hint;
  }

  if (wifi_state == WIFI_STATE_CONNECTING) {
    return F("Trying saved WiFi settings for up to 30 seconds. If it fails, the bridge switches to AP mode automatically.");
  }

  String hint = F("Connect to <strong>");
  hint += htmlEscape(String(access_point_ssid));
  hint += F("</strong> with password <strong>");
  hint += htmlEscape(String(WIFI_AP_PASSWORD));
  hint += F("</strong>, then open <strong>http://192.168.4.1/</strong>.");
  return hint;
}

String ipAddressValue(const char* value) {
  return htmlEscape(String(value));
}

void sendUiPage(AsyncWebServerRequest *request, const String& message, int status_code) {
  if (!littlefs_ready || !LittleFS.exists("/www/index.html")) {
    request->send(500, "text/plain", "LittleFS UI is missing\n");
    return;
  }

  AsyncWebServerResponse *response = request->beginResponse(
    LittleFS,
    "/www/index.html",
    "text/html; charset=utf-8",
    false,
    [message](const String& var) -> String {
      if (var == "MESSAGE_BLOCK") {
        if (!message.length()) {
          return "";
        }

        String block = F("<div class='ok'>");
        block += htmlEscape(message);
        block += F("</div>");
        return block;
      }

      if (var == "WIFI_STATE") {
        return wifiStateLabel();
      }

      if (var == "NETWORK_HINT") {
        return networkHint();
      }

      if (var == "SSID_VALUE") {
        return htmlEscape(String(wifi_settings.ssid));
      }

      if (var == "PASSWORD_VALUE") {
        return htmlEscape(String(wifi_settings.password));
      }

      if (var == "USE_STATIC_IP_CHECKED") {
        return wifi_settings.use_static_ip ? String(F("checked")) : String("");
      }

      if (var == "IP_VALUE") {
        return ipAddressValue(wifi_settings.ip);
      }

      if (var == "GATEWAY_VALUE") {
        return ipAddressValue(wifi_settings.gateway);
      }

      if (var == "SUBNET_VALUE") {
        return ipAddressValue(wifi_settings.subnet);
      }

      if (var == "DNS_VALUE") {
        return ipAddressValue(wifi_settings.dns);
      }

      return "";
    }
  );

  response->setCode(status_code);
  request->send(response);
}

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    sendUiPage(request);
  });

  server.on("/ui", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->redirect("/");
  });

  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
    String message = "";
    if (request->hasArg("saved")) {
      message = "Settings saved. Device will reboot.";
    } else if (request->hasArg("message") && request->arg("message") == "action_failed") {
      message = "Action failed.";
    }
    sendUiPage(request, message);
  });

  server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasArg("ssid")) {
      request->send(400, "text/plain", "SSID is required\n");
      return;
    }

    String ssid = request->arg("ssid");
    String password = request->hasArg("password") ? request->arg("password") : "";
    bool use_static_ip = request->hasArg("use_static_ip");
    String ip = request->hasArg("ip") ? request->arg("ip") : "";
    String gateway = request->hasArg("gateway") ? request->arg("gateway") : "";
    String subnet = request->hasArg("subnet") ? request->arg("subnet") : "";
    String dns = request->hasArg("dns") ? request->arg("dns") : "";
    ip.trim();
    gateway.trim();
    subnet.trim();
    dns.trim();

    if (!saveWiFiSettings(ssid, password, use_static_ip, ip, gateway, subnet, dns)) {
      sendUiPage(request, "Unable to save WiFi settings.", 400);
      return;
    }

    scheduleReboot();
    sendUiPage(request, "Settings saved. Device will reboot.");
  });

  server.on("/command", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("c")) {
      request->send(400, "text/plain", "/command?c=command_id[&a=device_address][&d=hex_data]");
      return;
    }

    if (!ensureClunetAvailable(request)) {
      return;
    }

    int command = request->getParam("c")->value().toInt();
    int address = request->hasParam("a") ? request->getParam("a")->value().toInt() : CLUNET_ADDRESS_BROADCAST;

    int dataLen = 0;
    char data[2 * CLUNET_PACKET_DATA_SIZE];
    if (request->hasParam("d")) {
      String hexData = request->getParam("d")->value();
      dataLen = hexStringToCharArray(data, (char*)hexData.c_str(), hexData.length());
    }

    clunet.send(address, command, data, dataLen);
    request->send(200, "text/plain", "OK");
  });

  server.on("/discovery", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!ensureClunetAvailable(request)) {
      return;
    }

    _request(request, CLUNET_ADDRESS_BROADCAST, CLUNET_COMMAND_DISCOVERY, NULL, 0, CLUNET_COMMAND_DISCOVERY_RESPONSE, 500);
  });

  server.on("/request", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("c")) {
      request->send(400, "text/plain", "/request?c=command_id[&a=device_address][&d=hex_data][&t=timeout_ms][&r=response_command_filter]");
      return;
    }

    if (!ensureClunetAvailable(request)) {
      return;
    }

    int command = request->getParam("c")->value().toInt();
    int address = request->hasParam("a") ? request->getParam("a")->value().toInt() : CLUNET_ADDRESS_BROADCAST;
    int responseTimeout = request->hasParam("t") ? request->getParam("t")->value().toInt() : 100;
    int responseCommand = request->hasParam("r") ? request->getParam("r")->value().toInt() : -1;

    int dataLen = 0;
    char data[2 * CLUNET_PACKET_DATA_SIZE];
    if (request->hasParam("d")) {
      String hexData = request->getParam("d")->value();
      dataLen = hexStringToCharArray(data, (char*)hexData.c_str(), hexData.length());
    }

    _request(request, address, command, data, dataLen, responseCommand, responseTimeout);
  });

  server.on("/api/switch", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!ensureClunetAvailable(request)) {
      return;
    }
    api_switch(request);
  });

  server.on("/api/info/switch", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!ensureClunetAvailable(request)) {
      return;
    }
    info_switch(request);
  });

  server.on("/api/dimmer", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!ensureClunetAvailable(request)) {
      return;
    }
    api_dimmer(request);
  });

  server.on("/api/fan", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!ensureClunetAvailable(request)) {
      return;
    }
    api_fan(request);
  });

  server.on("/api/info/fan", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!ensureClunetAvailable(request)) {
      return;
    }
    info_fan(request);
  });

  server.on("/api/door", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!ensureClunetAvailable(request)) {
      return;
    }
    api_door(request);
  });

  server.on("/registry/commands", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(4096);
    clunet_command cc;
    for (const auto c : CLUNET_COMMANDS) {
      memcpy_P(&cc, &c, sizeof(clunet_command));
      doc[String(c.code)] = FPSTR(c.name);
    }

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });

  server.on("/registry/devices", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(2048);
    clunet_device cd;
    for (const auto d : CLUNET_DEVICES) {
      memcpy_P(&cd, &d, sizeof(clunet_device));
      doc[String(d.code)] = FPSTR(d.name);
    }

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
  });

  server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->redirect("/log.html");
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    scheduleReboot();
    request->send(200, "text/plain", "Reboot scheduled\n");
  });

  events.onConnect([](AsyncEventSourceClient *client) {
    client->send("Welcome", "SERVICE", 0, 3000);
  });

  server.addHandler(&events);
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  if (littlefs_ready) {
    server.serveStatic("/log.html", LittleFS, "/www/log.html");
  }

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (wifi_state == WIFI_STATE_AP_MODE) {
      request->redirect("/");
      return;
    }
    request->send(404);
  });

  server.begin();
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("\n\nHello");

  Serial.begin(38400, SERIAL_8N1);
  Serial1.println("Booting");

  EEPROM.begin(sizeof(WifiSettings));
  loadWiFiSettings();

  littlefs_ready = LittleFS.begin();
  if (!littlefs_ready) {
    Serial1.println("LittleFS mount failed");
  }

  pinMode(LED_BLUE_PORT, OUTPUT);  
  analogWrite(LED_BLUE_PORT, 12);

  Serial.swap();
  Serial.flush();
  Serial.setRxBufferSize(256);  //as default

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname("smarthome-bridge");

  snprintf(access_point_ssid, sizeof(access_point_ssid), "%s-%06X", WIFI_AP_SSID_PREFIX, ESP.getChipId());

  ArduinoOTA.setHostname("smarthome-bridge");
  ArduinoOTA.onStart([]() {
    Serial1.println("ArduinoOTA start update");
    if (ArduinoOTA.getCommand() == U_FS && littlefs_ready) {
      LittleFS.end();
    }
  });

  setupWebServer();

  if (hasSavedWiFiSettings()) {
    startStationConnection();
  } else {
    Serial1.println("WiFi credentials not found, starting AP mode");
    startAccessPoint();
  }
}

char check_crc(char* data, uint8_t size){
  uint8_t crc=0;
  for (uint8_t i=0; i<size; i++){
    uint8_t inbyte = data[i];
    for (uint8_t j=0; j<8; j++){
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix){
        crc ^= 0x8C;
      }
      inbyte >>= 1;
    }
  }
  return crc;
}

void on_uart_message(uint8_t code, char* data, uint8_t length){
  switch(code){
    case UART_MESSAGE_CODE_CLUNET:
      if (data && length >= 4){
        clunet_packet* packet = (clunet_packet*)data;
        if (!CLUNET_MULTICAST_DEVICE(packet->src)){
           multicastQueue.add(packet->copy());
        }
      }
      break;
    case UART_MESSAGE_CODE_FIRMWARE:
      if (data && length >= 4){
        clunet_packet* packet = (clunet_packet*)data;
      }
      break;
    case UART_MESSAGE_CODE_DEBUG:
      Serial1.println("debug message received: " + String((int8_t)data[0]));
      break;
  }
}

#define UART_RX_BUF_LENGTH 256
volatile char uart_rx_data[UART_RX_BUF_LENGTH];
volatile unsigned char uart_rx_data_len = 0;

void analyze_uart_rx_trim(uint8_t offset){
  if (offset <= uart_rx_data_len){
    uart_rx_data_len -= offset;
    if (uart_rx_data_len){
      memmove((void*)uart_rx_data, (void*)(uart_rx_data + offset), uart_rx_data_len);
    }
  }
}

void analyze_uart_rx(void(*f)(uint8_t code, char* data, uint8_t length)){
  while (uart_rx_data_len > 1){
    //Serial1.println("len: " + uart_rx_data_len);
    uint8_t uart_rx_preambula_offset = uart_rx_data_len - 1;  //первый байт преамбулы может быть прочитан, а второй еще не пришел
    for (uint8_t i=0; i < uart_rx_data_len - 1; i++){
      if (uart_rx_data[i+0] == UART_MESSAGE_PREAMBULE[0] && uart_rx_data[i+1] == UART_MESSAGE_PREAMBULE[1]){
        uart_rx_preambula_offset = i;
        break;
      }
    }
    if (uart_rx_preambula_offset) {
      analyze_uart_rx_trim(uart_rx_preambula_offset ); //обрезаем мусор до преамбулы
    }

    if (uart_rx_data_len >= 5){ //минимальная длина сообщения с преамбулой
      char* uart_rx_message = (char*)(uart_rx_data + 2);
      uint8_t length = uart_rx_message[0];
      if (length < 3 || length > UART_RX_BUF_LENGTH){
        //Serial1.println("invalid length");
        analyze_uart_rx_trim(2);  //пришел мусор, отрезаем преамбулу и надо пробовать искать преамбулу снова
        continue;
      }
          
      if (uart_rx_data_len >= length+2){    //в буфере данных уже столько, сколько описано в поле length
        if (check_crc(uart_rx_message, length - 1) == uart_rx_message[length - 1]){ //проверка crc
          //Serial1.println("crc ok");
          if (f){
            //Serial1.println("Uart message received: code: " + String((int)uart_rx_message[1]) + "; length: " + String(length - 3));
            f(uart_rx_message[1], &uart_rx_message[2], length - 3);
          }
          
          analyze_uart_rx_trim(length+2); //отрезаем прочитанное сообщение
          //Serial1.println("uart_rx_data_len: " + String(uart_rx_data_len));
        }else{
          //Serial1.println("crc error");
          analyze_uart_rx_trim(2); 
        }
      }else{
        //Serial1.println("not enough data: " + String(uart_rx_data_len));
        break;
      }
    }else{
      //Serial1.println("too short message yet (length<5)");
      break;
    }
  }
}

void fillMessageData(JsonObject doc, clunet_packet* packet){
    if (packet->size){
      int hexLen = 0;
      char hex[256];
      hexLen = charArrayToHexString(hex, packet->data, packet->size);
      doc["hex"] = String(hex);

      fillObjData(doc.createNestedObject("obj"), packet);
    }
}

void fillObjData(JsonObject obj, clunet_packet* packet){
    char buf[512];
        
    switch(packet->command){
      case CLUNET_COMMAND_TEMPERATURE_INFO:{
        getTemperatureInfo(packet->data, buf);
        temperature_info* ti =(temperature_info*)buf;
        JsonArray sensors = obj.createNestedArray("sensors");
        for (int i=0; i<ti->num_sensors; i++){
          JsonObject sensor = sensors.createNestedObject();
          sensor["type"] = static_cast<int>(ti->sensors[i].type);
          sensor["id"] = ti->sensors[i].id;
          sensor["val"] = serialized(String(ti->sensors[i].value, 2));
        }
      }
      break;
      case CLUNET_COMMAND_HUMIDITY_INFO:{
        getHumidityInfo(packet->data, buf);
        humidity_info* hi =(humidity_info*)buf;
        obj["val"] = serialized(String(hi->value, 2)); 
      }
      break;
      case CLUNET_COMMAND_SWITCH_INFO:{
        JsonArray switches = obj.createNestedArray("switches");
        for (int i=0; i<8; i++){
          if (packet->data[0] & (1<<i)){
            JsonObject _switch = switches.createNestedObject();
            _switch["id"] = i+1;
            _switch["val"] = 1;
          }
        }
      }
      break;
    }
}

void fillValueData(JsonObject obj, clunet_packet* packet, char* cid){
    char buf[512];
    switch(packet->command){
      case CLUNET_COMMAND_TEMPERATURE_INFO:{
        getTemperatureInfo(packet->data, buf);
        temperature_info* ti =(temperature_info*)buf;
        for (int i=0; i<ti->num_sensors; i++){
          if (strcmp(ti->sensors[i].id, cid)){
            obj["type"] = static_cast<int>(ti->sensors[i].type);
            obj["cid"] = ti->sensors[i].id;
            obj["val"] = serialized(String(ti->sensors[i].value, 2));  
            break;
          }
        }
      }
      break;
      case CLUNET_COMMAND_HUMIDITY_INFO:{
        getHumidityInfo(packet->data, buf);
        humidity_info* hi =(humidity_info*)buf;
        obj["val"] = serialized(String(hi->value, 2)); 
      }
      break;
      case CLUNET_COMMAND_SWITCH_INFO: {
        int intId = String(cid).toInt();
        obj["cid"] = cid;
        obj["value"] = (bool)(packet->data[0] & (1<<(intId-1)));
      }
      break;
      case CLUNET_COMMAND_FAN_INFO: {
        obj["mode"] = static_cast<int>(packet->data[0]);
        obj["value"] = (bool)(packet->data[1] == 3 || packet->data[1] == 4);
      }
      break;
    }
}

void fillMessageJsonObject(JsonObject doc, uint32_t timestamp_sec, uint16_t timestamp_ms, clunet_packet* packet){
    doc["c"] = packet->command;
    doc["s"] = packet->src;
    doc["d"] = packet->dst;

    if (timestamp_sec){
      char buf[4];
      sprintf(buf, "%03d", timestamp_ms);
      doc["t"] = String(timestamp_sec) + String(buf);
    }

    if (packet->size){
      fillMessageData(doc.createNestedObject("m"), packet);
    }
}

#define DELAY_BETWEEN_UART_MESSAGES 5
long uart_time = 0;

void loop() {
  unsigned long now = millis();

  while (Serial.available() > 0 && uart_rx_data_len < UART_RX_BUF_LENGTH) {
    uart_rx_data[uart_rx_data_len++] = Serial.read();
  }
  analyze_uart_rx(on_uart_message);

  while (!uartQueue.isEmpty() && uart_can_send(uartQueue.front())){
    if (now - uart_time > DELAY_BETWEEN_UART_MESSAGES){
     uart_time = now;
     clunet_packet* packet = uartQueue.front();
     uart_send_message(packet);
     uartQueue.remove(packet);
    }
  }

  while (!multicastQueue.isEmpty()){
    clunet_packet* packet = multicastQueue.front();
    if (clunet_ready) {
      clunet.send_fake(packet->src, packet->dst, packet->command, packet->data, packet->size);
    }
    multicastQueue.remove(packet);
  }
  
  if (!eventsQueue.isEmpty() && events.avgPacketsWaiting()==0){
    DynamicJsonDocument doc(4196);
    while (!eventsQueue.isEmpty()){
      ts_clunet_packet* tp = eventsQueue.front();
      fillMessageJsonObject(doc.createNestedObject(), tp->timestamp_sec, tp->timestamp_ms, tp->packet);
      eventsQueue.remove(tp);
    }
    
    String json;
    serializeJson(doc, json);
    events.send(json.c_str(), "DATA", ++event_id);
  }

  updateWiFiState();

  if (apiResponse == NULL){
    while (!apiRequestsQueue.isEmpty()){
      api_request* ar = apiRequestsQueue.front();

      if (!clunet_ready) {
        if (ar->webRequest != NULL) {
          ar->webRequest->send(503, "text/plain", "Clunet is unavailable because WiFi is not connected\n");
        }
        apiRequestsQueue.remove(ar);
        continue;
      }

      if (ar->webRequest != NULL){
        
        apiResponse = (api_response*)malloc(sizeof(api_response));
        apiResponse->webRequest = ar->webRequest;
        apiResponse->info = ar->info;
        if (apiResponse->info){
          strcpy(apiResponse->infoId, ar->infoId);
        }
        apiResponse->responseFilterCommand = ar->responseFilterCommand;
        apiResponse->requestId = clunet.request(ar->address, ar->command, ar->data, ar->size, [](clunet_packet* packet){
            return apiResponse->responseFilterCommand < 0 || packet->command==apiResponse->responseFilterCommand;
        }, ar->responseTimeout);
        
        if (apiResponse->requestId){
          apiResponse->webRequest->onDisconnect([](){
            if (apiResponse != NULL){
              apiResponse->webRequest = NULL;
            }
          });
       
          apiRequestsQueue.remove(ar);
        }else{
          delete apiResponse;
          apiResponse = NULL;   
        }

        break;
      }else{
        apiRequestsQueue.remove(ar);
      }
    }
  }
  
  if (wifi_state == WIFI_STATE_STA_CONNECTED && ota_ready) {
    ArduinoOTA.handle();
  }

  if (reboot_scheduled_at && (long)(now - reboot_scheduled_at) >= 0) {
    ESP.restart();
  }

  yield();
}
