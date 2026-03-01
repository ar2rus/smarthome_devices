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
#include <TZ.h>

#include <DNSServer.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ClunetMulticast.h>

#include "KitchenLight.h"

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
ClunetMulticast clunet(CLUNET_DEVICE_ID, CLUNET_DEVICE_NAME);
DNSServer dns_server;

WifiSettings wifi_settings = {};
WifiState wifi_state = WIFI_STATE_IDLE;

int button_state;
int light_state = LOW;
int dimmer_value = 0;

bool server_started = false;
bool ota_ready = false;
bool clunet_ready = false;
bool clunet_handlers_registered = false;
bool littlefs_ready = false;

unsigned long fade_in_start_time = 0;
unsigned long button_pressed_time = 0;
unsigned long wifi_connect_started_at = 0;
unsigned long clunet_connect_attempt_at = 0;
unsigned long reboot_scheduled_at = 0;

char access_point_ssid[32] = {};

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

void setupWebServer();
void setupClunetCallbacks();
void updateWiFiState();
void startStationConnection();
void handleStationConnected();
void startAccessPoint();
void ensureClunetConnected();
void scheduleReboot();
void sendActionResponse(AsyncWebServerRequest *request, int response_code);

String htmlEscape(const String& value);
String wifiStateLabel();
String networkHint();
String ipAddressValue(const char* value);
void sendUiPage(AsyncWebServerRequest *request, const String& message = "", int status_code = 200);

void server_response(AsyncWebServerRequest *request, unsigned int response);
void switchResponse(unsigned char address);
bool switchExecute(byte command);
bool switch_exec(byte command, bool send_response);
bool switch_on(bool send_response);
bool switch_off(bool send_response);
bool switch_toggle(bool send_response);
void dimmerResponse(unsigned char address);
bool dimmerExecute(int value);
bool dimmer_exec(int value, bool send_response);
bool fade_in_start();
bool fade_in_stop(bool send_response);
void buttonResponse(unsigned char address);

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");

  pinMode(BUTTON_PIN, INPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, !light_state);

  button_state = digitalRead(BUTTON_PIN);

  analogWriteRange(PWM_RANGE);
  analogWriteFreq(PWM_FREQUENCY);

  EEPROM.begin(sizeof(WifiSettings));
  loadWiFiSettings();
  littlefs_ready = LittleFS.begin();

  if (!littlefs_ready) {
    Serial.println("LittleFS mount failed");
  }

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname("kitchen-light");

  snprintf(access_point_ssid, sizeof(access_point_ssid), "%s-%06X", WIFI_AP_SSID_PREFIX, ESP.getChipId());

  ArduinoOTA.setHostname("kitchen-light");
  ArduinoOTA.onStart([]() {
    Serial.println("ArduinoOTA start update");
  });

  setupWebServer();

  if (hasSavedWiFiSettings()) {
    startStationConnection();
  } else {
    Serial.println("WiFi credentials not found, starting AP mode");
    startAccessPoint();
  }
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

void setupWebServer() {
  if (server_started) {
    return;
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (wifi_state == WIFI_STATE_AP_MODE || request->hasArg("ui")) {
      sendUiPage(request);
      return;
    }

    int r = 404;
    if (request->args() == 0 && switch_toggle(true)) {
      r = 200;
    }
    server_response(request, r);
  });

  server.on("/ui", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendUiPage(request);
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool ui_request = request->hasArg("ui");
    int r = 404;
    if ((request->args() == 0 || (ui_request && request->args() == 1)) && switch_toggle(true)) {
      r = 200;
    }
    sendActionResponse(request, r);
  });

  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool ui_request = request->hasArg("ui");
    int r = 404;

    switch (request->args()) {
      case 0:
        if (switch_on(true)) {
          r = 200;
        }
        break;
      case 1:
        if (ui_request && switch_on(true)) {
          r = 200;
        } else if (request->hasArg("d")) {
          String arg = request->arg("d");
          byte num_digits = 0;

          for (byte i = 0; i < arg.length(); i++) {
            if (isDigit(arg.charAt(i))) {
              num_digits++;
            } else {
              num_digits = 0;
              break;
            }
          }

          r = 400;
          if (num_digits && num_digits <= 3 && dimmer_exec(arg.toInt(), true)) {
            r = 200;
          }
        }
        break;
    }

    sendActionResponse(request, r);
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool ui_request = request->hasArg("ui");
    int r = 404;
    if ((request->args() == 0 || (ui_request && request->args() == 1)) && switch_off(true)) {
      r = 200;
    }
    sendActionResponse(request, r);
  });

  server.on("/fadein", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool ui_request = request->hasArg("ui");
    int r = 404;
    if ((request->args() == 0 || (ui_request && request->args() == 1)) && fade_in_start()) {
      r = 200;
    }
    sendActionResponse(request, r);
  });

  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    String message = "";
    if (request->hasArg("saved")) {
      message = "Settings saved. Device will reboot.";
    } else if (request->hasArg("message") && request->arg("message") == "action_failed") {
      message = "Action failed.";
    }
    sendUiPage(request, message);
  });

  server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
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

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    scheduleReboot();
    request->send(200, "text/plain", "Reboot scheduled\n");
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (wifi_state == WIFI_STATE_AP_MODE) {
      request->redirect("/");
      return;
    }
    server_response(request, 404);
  });

  server.begin();
  server_started = true;
}

void setupClunetCallbacks() {
  if (clunet_handlers_registered) {
    return;
  }

  clunet.onPacketReceived([](clunet_packet* packet) {
    switch (packet->command) {
      case CLUNET_COMMAND_SWITCH:
        if (packet->data[0] == 0xFF) {
          if (packet->size == 1) {
            switchResponse(packet->src);
          }
        } else if (packet->size == 2) {
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
          if ((packet->data[0] >> (RELAY_0_ID - 1)) & 0x01) {
            dimmer_exec(packet->data[1], false);
            dimmerResponse(packet->src);
          }
        }
        break;
    }
  });

  clunet_handlers_registered = true;
}

void startStationConnection() {
  if (!hasSavedWiFiSettings()) {
    startAccessPoint();
    return;
  }

  Serial.print("Connecting to WiFi: ");
  Serial.println(wifi_settings.ssid);

  dns_server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  if (!configureStationNetwork()) {
    Serial.println("Invalid static IP settings, falling back to AP mode");
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

  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

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

  Serial.println("Starting AP mode");

  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(access_point_ssid, WIFI_AP_PASSWORD);
  dns_server.start(53, "*", WiFi.softAPIP());

  wifi_state = WIFI_STATE_AP_MODE;
  wifi_connect_started_at = 0;
  ota_ready = false;
  clunet_ready = false;

  Serial.print("AP SSID: ");
  Serial.println(access_point_ssid);
  Serial.print("AP password: ");
  Serial.println(WIFI_AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
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
    Serial.println("Clunet connected");
  }
}

void updateWiFiState() {
  wl_status_t status = WiFi.status();

  switch (wifi_state) {
    case WIFI_STATE_CONNECTING:
      if (status == WL_CONNECTED) {
        handleStationConnected();
      } else if (millis() - wifi_connect_started_at >= WIFI_CONNECT_TIMEOUT_MS) {
        Serial.println("WiFi connection timeout, switching to AP mode");
        startAccessPoint();
      }
      break;
    case WIFI_STATE_STA_CONNECTED:
      if (status != WL_CONNECTED) {
        Serial.println("WiFi lost, reconnecting");
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

void sendActionResponse(AsyncWebServerRequest *request, int response_code) {
  if (request->hasArg("ui")) {
    String location = "/ui";
    if (response_code != 200) {
      location += "?message=action_failed";
    }
    request->redirect(location);
    return;
  }

  server_response(request, response_code);
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
    String hint = F("Connected as client. OTA is available from your local network.<br>IP: ");
    hint += WiFi.localIP().toString();
    hint += F("<br>Saved SSID: ");
    hint += htmlEscape(String(wifi_settings.ssid));
    hint += F("<br>Addressing: ");
    hint += wifi_settings.use_static_ip ? F("Static IP") : F("DHCP");
    return hint;
  }

  if (wifi_state == WIFI_STATE_CONNECTING) {
    return F("Trying saved WiFi settings for up to 30 seconds. If it fails, device switches to AP mode automatically.");
  }

  String hint = F("Connect your phone to <strong>");
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
  if (!littlefs_ready || !LittleFS.exists("/index.html")) {
    request->send(500, "text/plain", "LittleFS UI is missing\n");
    return;
  }

  AsyncWebServerResponse *response = request->beginResponse(
    LittleFS,
    "/index.html",
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

      if (var == "LIGHT_STATE") {
        return light_state ? F("On") : F("Off");
      }

      if (var == "DIMMER_VALUE") {
        return String(dimmer_value);
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

void switchResponse(unsigned char address) {
  if (!clunet_ready) {
    return;
  }

  char info = (light_state << (RELAY_0_ID - 1));
  clunet.send(address, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

bool switchExecute(byte command) {
  switch (command) {
    case 0x00:
      light_state = LOW;
      break;
    case 0x01:
      light_state = HIGH;
      break;
    case 0x02:
      light_state = !light_state;
      break;
    default:
      return false;
  }

  dimmer_value = 0;
  analogWrite(LIGHT_PIN, dimmer_value);
  digitalWrite(LIGHT_PIN, !light_state);
  return true;
}

bool switch_exec(byte command, bool send_response) {
  bool r = switchExecute(command);
  if (r) {
    if (send_response) {
      switchResponse(CLUNET_ADDRESS_BROADCAST);
    }
    fade_in_stop(false);
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
  if (!clunet_ready) {
    return;
  }

  char data[] = {1, RELAY_0_ID, (char)dimmer_value};
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
  bool r = dimmerExecute(value);
  if (r && send_response) {
    dimmerResponse(CLUNET_ADDRESS_BROADCAST);
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
  if (!clunet_ready) {
    return;
  }

  char data[] = {BUTTON_ID, (char)!button_state};
  clunet.send(address, CLUNET_COMMAND_BUTTON_INFO, data, sizeof(data));
}

void loop() {
  int button_tmp = digitalRead(BUTTON_PIN);
  unsigned long m = millis();

  if (button_state != button_tmp) {
    if (button_tmp == LOW) {
      if (!button_pressed_time) {
        button_pressed_time = m;
      }
      if (m - button_pressed_time >= DELAY_BEFORE_TOGGLE) {
        button_state = button_tmp;
        buttonResponse(CLUNET_ADDRESS_BROADCAST);
        switch_toggle(true);
        if (!light_state) {
          button_pressed_time = 0;
        }
      }
    } else {
      button_state = button_tmp;
      buttonResponse(CLUNET_ADDRESS_BROADCAST);

      if (button_pressed_time) {
        fade_in_stop(true);
      }
    }
  }

  if (button_tmp == HIGH) {
    button_pressed_time = 0;
  }

  if (button_pressed_time && m - button_pressed_time >= DELAY_BEFORE_PWM) {
    fade_in_start();
  }

  if (fade_in_start_time) {
    int v0 = (m - fade_in_start_time) % PWM_DOWN_UP_CYCLE_TIME;
    int v1 = v0 % PWM_DOWN_UP_CYCLE_TIME_2;
    if (v0 >= PWM_DOWN_UP_CYCLE_TIME_2) {
      dimmer_exec(PWM_RANGE * v1 / (PWM_DOWN_UP_CYCLE_TIME_2 - 1), false);
    } else {
      dimmer_exec(PWM_RANGE - PWM_RANGE * v1 / (PWM_DOWN_UP_CYCLE_TIME_2 - 1), false);
    }
  }

  updateWiFiState();

  if (wifi_state == WIFI_STATE_STA_CONNECTED && ota_ready) {
    ArduinoOTA.handle();
  }

  if (reboot_scheduled_at && (long)(m - reboot_scheduled_at) >= 0) {
    ESP.restart();
  }

  yield();
}
