#include "Config.h"

#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

namespace BridgeConfig {

static constexpr uint32_t SETTINGS_MAGIC = 0x53424857UL;
static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 60000UL;
static constexpr unsigned long WIFI_STA_RECONNECT_DELAY_MS = 1500UL;
static constexpr unsigned long WIFI_AP_RESTART_TIMEOUT_MS = 15UL * 60UL * 1000UL;
static constexpr unsigned long CONFIG_REBOOT_DELAY_MS = 1500UL;
static constexpr const char* WIFI_HOSTNAME = "smarthome-bridge";
static constexpr const char* WIFI_AP_SSID_PREFIX = "SmarthomeBridge";

enum WifiState : uint8_t {
  WIFI_STATE_IDLE = 0,
  WIFI_STATE_CONNECTING = 1,
  WIFI_STATE_STA_CONNECTED = 2,
  WIFI_STATE_AP_MODE = 3
};

typedef struct {
  const char* id;
  const char* label;
  const char* tz;
} timezone_option;

static const timezone_option TIMEZONE_OPTIONS[] = {
  {"utc", "UTC", "UTC0"},
  {"new_york", "America/New_York (UTC-5/-4)", "EST5EDT,M3.2.0/2,M11.1.0/2"},
  {"london", "Europe/London (UTC+0/+1)", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"tokyo", "Asia/Tokyo (UTC+9)", "<+09>-9"},
  {"los_angeles", "America/Los_Angeles (UTC-8/-7)", "PST8PDT,M3.2.0/2,M11.1.0/2"},
  {"berlin", "Europe/Berlin (UTC+1/+2)", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"moscow", "Europe/Moscow (UTC+3)", "<+03>-3"},
  {"samara", "Europe/Samara (UTC+4)", "<+04>-4"},
  {"dubai", "Asia/Dubai (UTC+4)", "<+04>-4"},
  {"singapore", "Asia/Singapore (UTC+8)", "<+08>-8"},
  {"sydney", "Australia/Sydney (UTC+10/+11)", "AEST-10AEDT,M10.1.0/2,M4.1.0/3"},
  {"chicago", "America/Chicago (UTC-6/-5)", "CST6CDT,M3.2.0/2,M11.1.0/2"},
  {"denver", "America/Denver (UTC-7/-6)", "MST7MDT,M3.2.0/2,M11.1.0/2"},
  {"phoenix", "America/Phoenix (UTC-7)", "MST7"},
  {"honolulu", "Pacific/Honolulu (UTC-10)", "HST10"},
  {"sao_paulo", "America/Sao_Paulo (UTC-3)", "<-03>3"},
  {"mexico_city", "America/Mexico_City (UTC-6)", "CST6"},
  {"toronto", "America/Toronto (UTC-5/-4)", "EST5EDT,M3.2.0/2,M11.1.0/2"},
  {"buenos_aires", "America/Argentina/Buenos_Aires (UTC-3)", "<-03>3"},
  {"reykjavik", "Atlantic/Reykjavik (UTC+0)", "GMT0"},
  {"lisbon", "Europe/Lisbon (UTC+0/+1)", "WET0WEST,M3.5.0/1,M10.5.0"},
  {"paris", "Europe/Paris (UTC+1/+2)", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"helsinki", "Europe/Helsinki (UTC+2/+3)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"istanbul", "Europe/Istanbul (UTC+3)", "<+03>-3"},
  {"cairo", "Africa/Cairo (UTC+2)", "EET-2"},
  {"johannesburg", "Africa/Johannesburg (UTC+2)", "SAST-2"},
  {"nairobi", "Africa/Nairobi (UTC+3)", "EAT-3"},
  {"delhi", "Asia/Delhi (UTC+5:30)", "<+0530>-5:30"},
  {"dhaka", "Asia/Dhaka (UTC+6)", "<+06>-6"},
  {"bangkok", "Asia/Bangkok (UTC+7)", "<+07>-7"},
  {"jakarta", "Asia/Jakarta (UTC+7)", "<+07>-7"},
  {"hong_kong", "Asia/Hong_Kong (UTC+8)", "<+08>-8"},
  {"shanghai", "Asia/Shanghai (UTC+8)", "<+08>-8"},
  {"seoul", "Asia/Seoul (UTC+9)", "<+09>-9"},
  {"auckland", "Pacific/Auckland (UTC+12/+13)", "NZST-12NZDT,M9.5.0/2,M4.1.0/3"},
  {"yekaterinburg", "Asia/Yekaterinburg (UTC+5)", "<+05>-5"},
  {"omsk", "Asia/Omsk (UTC+6)", "<+06>-6"},
  {"krasnoyarsk", "Asia/Krasnoyarsk (UTC+7)", "<+07>-7"},
  {"irkutsk", "Asia/Irkutsk (UTC+8)", "<+08>-8"},
  {"yakutsk", "Asia/Yakutsk (UTC+9)", "<+09>-9"},
  {"vladivostok", "Asia/Vladivostok (UTC+10)", "<+10>-10"},
  {"kamchatka", "Asia/Kamchatka (UTC+12)", "<+12>-12"}
};

typedef struct {
  uint32_t magic;
  char ssid[33];
  char password[65];
  uint8_t use_static_ip;
  char ip[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
  char timezone_id[24];
} config_settings_t;

static config_settings_t settings = {};
static WifiState wifiState = WIFI_STATE_IDLE;
static DNSServer dnsServer;
static unsigned long wifiConnectStartedAt = 0;
static unsigned long wifiStaReconnectAt = 0;
static unsigned long apModeStartedAt = 0;
static unsigned long rebootScheduledAt = 0;
static char accessPointSsid[32] = {};

static bool parseIpAddress(const char* value, IPAddress& out){
  return value && value[0] && out.fromString(value);
}

static String requestValue(AsyncWebServerRequest* request, const char* key){
  if (request->hasParam(key, true)){
    return request->getParam(key, true)->value();
  }
  if (request->hasParam(key)){
    return request->getParam(key)->value();
  }
  return String();
}

static bool requestBool(AsyncWebServerRequest* request, const char* key){
  String value = requestValue(request, key);
  value.toLowerCase();
  return value == "1" || value == "true" || value == "on" || value == "yes";
}

static String htmlEscapeValue(const String& value){
  String escaped;
  escaped.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); i++){
    char c = value.charAt(i);
    switch (c){
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += c; break;
    }
  }
  return escaped;
}

static String normalizedApIp(){
  String apIp = WiFi.softAPIP().toString();
  if (apIp.length() >= 2 && apIp[0] == '(' && apIp[apIp.length() - 1] == ')'){
    apIp = apIp.substring(1, apIp.length() - 1);
  }
  if (!apIp.length()){
    apIp = "IP unset";
  }
  return apIp;
}

static const timezone_option* timezoneOptionById(const char* id){
  if (!id || !id[0]){
    return nullptr;
  }
  for (size_t i = 0; i < sizeof(TIMEZONE_OPTIONS) / sizeof(TIMEZONE_OPTIONS[0]); i++){
    if (strcmp(TIMEZONE_OPTIONS[i].id, id) == 0){
      return &TIMEZONE_OPTIONS[i];
    }
  }
  return nullptr;
}

static const timezone_option* activeTimezoneOption(){
  const timezone_option* option = timezoneOptionById(settings.timezone_id);
  return option;
}

static bool hasSavedWiFiSettings(){
  return settings.magic == SETTINGS_MAGIC && settings.ssid[0];
}

static const char* wifiStateName(WifiState state){
  switch (state){
    case WIFI_STATE_CONNECTING: return "connecting";
    case WIFI_STATE_STA_CONNECTED: return "sta_connected";
    case WIFI_STATE_AP_MODE: return "ap_mode";
    case WIFI_STATE_IDLE:
    default:
      return "idle";
  }
}

static bool configureStationNetwork(){
  if (!settings.use_static_ip){
    IPAddress noip(0, 0, 0, 0);
    return WiFi.config(noip, noip, noip);
  }

  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
  if (!parseIpAddress(settings.ip, ip) ||
      !parseIpAddress(settings.gateway, gateway) ||
      !parseIpAddress(settings.subnet, subnet)){
    return false;
  }
  if (!parseIpAddress(settings.dns, dns)){
    dns = gateway;
  }
  return WiFi.config(ip, gateway, subnet, dns);
}

static bool loadSettings(){
  EEPROM.get(0, settings);
  if (settings.magic != SETTINGS_MAGIC){
    memset(&settings, 0, sizeof(settings));
    return false;
  }

  settings.ssid[sizeof(settings.ssid) - 1] = 0;
  settings.password[sizeof(settings.password) - 1] = 0;
  settings.ip[sizeof(settings.ip) - 1] = 0;
  settings.gateway[sizeof(settings.gateway) - 1] = 0;
  settings.subnet[sizeof(settings.subnet) - 1] = 0;
  settings.dns[sizeof(settings.dns) - 1] = 0;
  settings.timezone_id[sizeof(settings.timezone_id) - 1] = 0;
  settings.use_static_ip = settings.use_static_ip ? 1 : 0;

  if (!settings.ssid[0]){
    memset(&settings, 0, sizeof(settings));
    return false;
  }

  if (settings.use_static_ip){
    IPAddress ip;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns;
    if (!parseIpAddress(settings.ip, ip) ||
        !parseIpAddress(settings.gateway, gateway) ||
        !parseIpAddress(settings.subnet, subnet) ||
        (settings.dns[0] && !parseIpAddress(settings.dns, dns))){
      settings.use_static_ip = 0;
      memset(settings.ip, 0, sizeof(settings.ip));
      memset(settings.gateway, 0, sizeof(settings.gateway));
      memset(settings.subnet, 0, sizeof(settings.subnet));
      memset(settings.dns, 0, sizeof(settings.dns));
    }
  }

  if (settings.timezone_id[0] && !timezoneOptionById(settings.timezone_id)){
    memset(settings.timezone_id, 0, sizeof(settings.timezone_id));
  }

  return true;
}

static bool saveSettings(const String& ssid, const String& password, bool useStaticIp,
                         const String& ip, const String& gateway, const String& subnet, const String& dns,
                         const String& timezoneId){
  if (!ssid.length() || ssid.length() >= sizeof(settings.ssid)){
    return false;
  }
  if (password.length() >= sizeof(settings.password)){
    return false;
  }
  if (ip.length() >= sizeof(settings.ip) ||
      gateway.length() >= sizeof(settings.gateway) ||
      subnet.length() >= sizeof(settings.subnet) ||
      dns.length() >= sizeof(settings.dns)){
    return false;
  }
  if (timezoneId.length() >= sizeof(settings.timezone_id) || !timezoneOptionById(timezoneId.c_str())){
    return false;
  }

  if (useStaticIp){
    IPAddress parsedIp;
    IPAddress parsedGateway;
    IPAddress parsedSubnet;
    IPAddress parsedDns;
    if (!parsedIp.fromString(ip) || !parsedGateway.fromString(gateway) || !parsedSubnet.fromString(subnet)){
      return false;
    }
    if (dns.length() && !parsedDns.fromString(dns)){
      return false;
    }
  }

  memset(&settings, 0, sizeof(settings));
  settings.magic = SETTINGS_MAGIC;
  settings.use_static_ip = useStaticIp ? 1 : 0;
  ssid.toCharArray(settings.ssid, sizeof(settings.ssid));
  password.toCharArray(settings.password, sizeof(settings.password));
  ip.toCharArray(settings.ip, sizeof(settings.ip));
  gateway.toCharArray(settings.gateway, sizeof(settings.gateway));
  subnet.toCharArray(settings.subnet, sizeof(settings.subnet));
  dns.toCharArray(settings.dns, sizeof(settings.dns));
  timezoneId.toCharArray(settings.timezone_id, sizeof(settings.timezone_id));

  EEPROM.put(0, settings);
  return EEPROM.commit();
}

static void clearSettings(){
  memset(&settings, 0, sizeof(settings));
  EEPROM.put(0, settings);
  EEPROM.commit();
}

static void startAccessPoint(){
  dnsServer.stop();
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(accessPointSsid);
  dnsServer.start(53, "*", WiFi.softAPIP());

  wifiState = WIFI_STATE_AP_MODE;
  wifiConnectStartedAt = 0;
  wifiStaReconnectAt = 0;
  apModeStartedAt = millis();
  rebootScheduledAt = 0;

  Serial1.print("AP mode: ");
  Serial1.println(accessPointSsid);
  Serial1.print("AP IP: ");
  Serial1.println(WiFi.softAPIP());
}

static void startStationConnection(){
  if (!hasSavedWiFiSettings()){
    startAccessPoint();
    return;
  }

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);

  if (!configureStationNetwork()){
    Serial1.println("WiFi static config invalid, fallback to AP");
    startAccessPoint();
    return;
  }

  if (settings.password[0]){
    WiFi.begin(settings.ssid, settings.password);
  } else {
    WiFi.begin(settings.ssid);
  }

  wifiState = WIFI_STATE_CONNECTING;
  wifiConnectStartedAt = millis();
  wifiStaReconnectAt = 0;
  apModeStartedAt = 0;
  rebootScheduledAt = 0;

  Serial1.print("Connecting WiFi SSID: ");
  Serial1.println(settings.ssid);
}

static void handleStationConnected(){
  if (wifiState == WIFI_STATE_STA_CONNECTED){
    return;
  }

  wifiState = WIFI_STATE_STA_CONNECTED;
  wifiConnectStartedAt = 0;
  wifiStaReconnectAt = 0;
  apModeStartedAt = 0;
  rebootScheduledAt = 0;
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  Serial1.print("WiFi connected: ");
  Serial1.println(WiFi.localIP());
}

static String configTemplateValue(const String& key){
  if (key == "WIFI_STATE"){
    return htmlEscapeValue(String(wifiStateName(wifiState)));
  }
  if (key == "WIFI_SAVED"){
    return hasSavedWiFiSettings() ? String("yes") : String("no");
  }
  if (key == "STA_IP"){
    return htmlEscapeValue(WiFi.localIP().toString());
  }
  if (key == "AP_SSID"){
    return htmlEscapeValue(String(accessPointSsid));
  }
  if (key == "AP_IP"){
    return htmlEscapeValue(normalizedApIp());
  }
  if (key == "WIFI_SSID"){
    return htmlEscapeValue(String(settings.ssid));
  }
  if (key == "WIFI_IP"){
    return htmlEscapeValue(String(settings.ip));
  }
  if (key == "WIFI_GATEWAY"){
    return htmlEscapeValue(String(settings.gateway));
  }
  if (key == "WIFI_SUBNET"){
    return htmlEscapeValue(String(settings.subnet));
  }
  if (key == "WIFI_DNS"){
    return htmlEscapeValue(String(settings.dns));
  }
  if (key == "USE_STATIC_IP_CHECKED"){
    return settings.use_static_ip ? String("checked") : String();
  }
  if (key == "TIMEZONE_ID"){
    return htmlEscapeValue(String(settings.timezone_id));
  }
  return String();
}

void sendPage(AsyncWebServerRequest* request){
  if (!LittleFS.exists("/www/config.html")){
    request->send(500, "text/html", "<!doctype html><html><body>config.html not found</body></html>");
    return;
  }

  AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/www/config.html", "text/html", false, configTemplateValue);
  if (!response){
    request->send(500, "text/plain", "config response init failed");
    return;
  }
  response->addHeader(F("Cache-Control"), F("no-store, no-cache, must-revalidate, max-age=0"));
  response->addHeader(F("Pragma"), F("no-cache"));
  response->addHeader(F("Expires"), F("0"));
  request->send(response);
}

void setupRoutes(AsyncWebServer& server){
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest* request){
    sendPage(request);
  });

  server.on("/config", HTTP_POST, [](AsyncWebServerRequest* request){
    String action = requestValue(request, "action");
    action.toLowerCase();

    if (action == "clear"){
      clearSettings();
      startAccessPoint();
      request->redirect("/?status=cleared");
      return;
    }

    String ssid = requestValue(request, "ssid");
    String password = requestValue(request, "password");
    String ip = requestValue(request, "ip");
    String gateway = requestValue(request, "gateway");
    String subnet = requestValue(request, "subnet");
    String dns = requestValue(request, "dns");
    String timezoneId = requestValue(request, "timezone");
    bool useStaticIp = requestBool(request, "useStaticIp");

    ssid.trim();
    ip.trim();
    gateway.trim();
    subnet.trim();
    dns.trim();
    timezoneId.trim();

    if (!password.length() && hasSavedWiFiSettings() && ssid == settings.ssid){
      password = String(settings.password);
    }
    if (!saveSettings(ssid, password, useStaticIp, ip, gateway, subnet, dns, timezoneId)){
      if (wifiState == WIFI_STATE_AP_MODE){
        request->redirect("/?status=invalid");
      } else {
        request->redirect("/config?status=invalid");
      }
      return;
    }

    startStationConnection();
    if (wifiState == WIFI_STATE_AP_MODE){
      request->redirect("/?status=saved");
    } else {
      request->redirect("/config?status=saved");
    }
  });

  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest* request){
    request->redirect("/config");
  });
}

void init(){
  EEPROM.begin(sizeof(config_settings_t));
  loadSettings();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(WIFI_HOSTNAME);
  snprintf(accessPointSsid, sizeof(accessPointSsid), "%s-%06X", WIFI_AP_SSID_PREFIX, ESP.getChipId());

  if (hasSavedWiFiSettings()){
    startStationConnection();
  } else {
    startAccessPoint();
  }
}

void loop(){
  if (rebootScheduledAt && static_cast<long>(millis() - rebootScheduledAt) >= 0){
    ESP.restart();
    return;
  }

  wl_status_t status = WiFi.status();
  switch (wifiState){
    case WIFI_STATE_CONNECTING:
      if (status == WL_CONNECTED){
        handleStationConnected();
      } else if (millis() - wifiConnectStartedAt >= WIFI_CONNECT_TIMEOUT_MS){
        Serial1.println("WiFi timeout, fallback to AP");
        startAccessPoint();
      }
      break;
    case WIFI_STATE_STA_CONNECTED:
      if (status != WL_CONNECTED){
        if (!wifiStaReconnectAt){
          wifiStaReconnectAt = millis() + WIFI_STA_RECONNECT_DELAY_MS;
        }
      }
      break;
    case WIFI_STATE_AP_MODE:
      dnsServer.processNextRequest();
      if (
        hasSavedWiFiSettings() &&
        rebootScheduledAt == 0 &&
        apModeStartedAt != 0 &&
        (millis() - apModeStartedAt) >= WIFI_AP_RESTART_TIMEOUT_MS
      ){
        rebootScheduledAt = millis() + CONFIG_REBOOT_DELAY_MS;
        Serial1.println("AP timeout, rebooting to retry STA");
      }
      break;
    case WIFI_STATE_IDLE:
    default:
      if (hasSavedWiFiSettings()){
        startStationConnection();
      } else {
        startAccessPoint();
      }
      break;
  }

  if (wifiStaReconnectAt && millis() >= wifiStaReconnectAt){
    startStationConnection();
  }
}

bool isApMode(){
  return wifiState == WIFI_STATE_AP_MODE;
}

bool isStaConnected(){
  return wifiState == WIFI_STATE_STA_CONNECTED && WiFi.status() == WL_CONNECTED;
}

const char* timezone(){
  const timezone_option* option = activeTimezoneOption();
  if (!option){
    return nullptr;
  }
  return option->tz;
}

void forceApMode(){
  Serial1.println("Forced AP mode");
  startAccessPoint();
}

} // namespace BridgeConfig
