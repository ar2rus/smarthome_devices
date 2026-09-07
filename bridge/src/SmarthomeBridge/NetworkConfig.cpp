#include "NetworkConfig.h"
#include "TimeZones.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <time.h>

namespace NetworkConfig {

static constexpr uint32_t SETTINGS_MAGIC = 0x53484257UL;
static constexpr uint16_t SETTINGS_VERSION = 1;
static constexpr uint32_t TIMEZONE_MAGIC = 0x5348545AUL;
static constexpr uint16_t TIMEZONE_VERSION = 1;
static constexpr size_t EEPROM_SIZE = 256;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 60000UL;
static constexpr uint32_t AP_RETRY_MS = 15UL * 60UL * 1000UL;
static constexpr uint32_t MODE_CHANGE_DELAY_MS = 500UL;
static constexpr uint32_t RESTART_DELAY_MS = 1200UL;
static constexpr size_t MAX_REQUEST_BODY = 1024;
static constexpr char AP_PASSWORD[] = "12345678";

struct __attribute__((packed)) StoredSettings {
  uint32_t magic;
  uint16_t version;
  char ssid[33];
  char password[65];
  uint8_t useStaticIp;
  char ip[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
  uint32_t crc;
};

struct __attribute__((packed)) StoredTimeZone {
  uint32_t magic;
  uint16_t version;
  char id[33];
  uint32_t crc;
};

static_assert(sizeof(StoredSettings) + sizeof(StoredTimeZone) <= EEPROM_SIZE, "EEPROM settings overflow");

static StoredSettings settings = {};
static StoredTimeZone storedTimeZone = {};
static State state = IDLE;
static DNSServer dnsServer;
static char apSsid[32] = {};
static uint32_t stateStartedAt = 0;
static uint32_t scheduledApAt = 0;
static uint32_t scheduledRestartAt = 0;
static uint32_t reconnects = 0;
static bool fsAvailable = false;
static bool (*allowTransition)() = nullptr;

static bool transitionAllowed() { return !allowTransition || allowTransition(); }

static const char FALLBACK_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmarthomeBridge</title><style>
body{font:14px Arial;margin:0;padding:16px;background:#f4f6f8;color:#1f2933}.layout{max-width:760px;margin:auto}.card{background:#fff;border-radius:12px;padding:16px;margin-bottom:16px;box-shadow:0 4px 18px #0f172a14}h1,h2{margin-top:0}label{display:block;font-weight:600;margin:10px 0 4px}input,select{box-sizing:border-box;width:100%;padding:10px;border:1px solid #cbd2d9;border-radius:8px;background:#fff}.grid,.status{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px}.status div{padding:12px;border-radius:8px;background:#f8fafc}.status strong{display:block;font-size:12px;text-transform:uppercase;color:#486581}.check{display:flex;gap:8px;align-items:center}.check input{width:auto}button{margin:14px 8px 0 0;padding:12px 16px;border:0;border-radius:8px;background:#243b53;color:#fff}button.secondary{background:#9fb3c8;color:#102a43}#msg{margin-top:12px;padding:10px;border-radius:8px;background:#fff7d6;color:#7c5e10;white-space:pre-wrap}
</style></head><body><div class="layout"><section class="card"><h1>Настройки SmarthomeBridge</h1><div id="msg">Загрузка…</div><div class="status">
<div><strong>Режим</strong><span id="mode">-</span></div><div><strong>AP SSID</strong><span id="apSsid">-</span></div><div><strong>AP пароль</strong>12345678</div><div><strong>Адрес</strong><span id="address">-</span></div></div></section>
<form id="f"><section class="card"><h2>WiFi</h2><div class="grid"><div><label>SSID</label><input id="ssid" maxlength="32" required></div><div><label>Новый пароль</label><input id="password" type="password" maxlength="64" placeholder="Пусто — сохранить прежний"></div></div>
<label class="check"><input id="clearPassword" type="checkbox">Очистить пароль (открытая сеть)</label>
<label class="check"><input id="static" type="checkbox">Статический IP</label><div class="grid"><div><label>IP</label><input id="ip"></div><div><label>Gateway</label><input id="gateway"></div><div><label>Mask</label><input id="subnet"></div><div><label>DNS</label><input id="dns"></div></div></section>
<section class="card"><h2>Часовой пояс</h2><label>Timezone</label><select id="timeZone"></select></section>
<section class="card"><button>Сохранить и перезагрузить</button><button class="secondary" id="ap" type="button">Перейти в режим точки доступа</button></section></form></div><script>
const $=id=>document.getElementById(id),msg=t=>$('msg').textContent=t;let original='';
function fields(){let off=!$('static').checked;['ip','gateway','subnet','dns'].forEach(x=>$(x).disabled=off)}
Promise.all([fetch('/api/timezones').then(r=>r.json()),fetch('/api/network/config').then(r=>r.json())]).then(x=>{let z=x[0],d=x[1];z.timeZones.forEach(v=>$('timeZone').add(new Option(v.label,v.id)));$('timeZone').value=d.timeZone;original=d.wifi.ssid||'';$('ssid').value=original;$('static').checked=!!d.wifi.useStaticIp;['ip','gateway','subnet','dns'].forEach(v=>$(v).value=d.wifi[v]||'');$('mode').textContent=d.network.mode;$('apSsid').textContent=d.network.apSsid;$('address').textContent=d.network.mode==='ap'?d.network.apIp:d.network.stationIp;fields();msg('')}).catch(e=>msg(e.message));
$('static').onchange=fields;$('f').onsubmit=async e=>{e.preventDefault();let wifi={ssid:$('ssid').value,useStaticIp:$('static').checked,ip:$('ip').value,gateway:$('gateway').value,subnet:$('subnet').value,dns:$('dns').value};if($('clearPassword').checked)wifi.password='';else if($('password').value)wifi.password=$('password').value;else if($('ssid').value!==original)wifi.password='';let r=await fetch('/api/network/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({wifi,timeZone:$('timeZone').value})});let d=await r.json();msg(d.message||'Ошибка');};
$('ap').onclick=async()=>{let r=await fetch('/api/network/ap',{method:'POST'});let d=await r.json();msg(d.message||'Переключение…')};
</script></body></html>)HTML";

static uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (length--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
  }
  return ~crc;
}

static uint32_t settingsCrc(const StoredSettings& value) {
  return crc32(reinterpret_cast<const uint8_t*>(&value), offsetof(StoredSettings, crc));
}

static uint32_t timeZoneCrc(const StoredTimeZone& value) {
  return crc32(reinterpret_cast<const uint8_t*>(&value), offsetof(StoredTimeZone, crc));
}

static bool parseIp(const char* text, IPAddress& result) {
  return text && *text && result.fromString(text) && result != IPAddress(0, 0, 0, 0);
}

static bool settingsValid(const StoredSettings& value) {
  if (value.magic != SETTINGS_MAGIC || value.version != SETTINGS_VERSION || value.crc != settingsCrc(value)) return false;
  if (!value.ssid[0] || value.ssid[sizeof(value.ssid) - 1] || value.password[sizeof(value.password) - 1]) return false;
  if (!value.useStaticIp) return true;
  IPAddress ip, gateway, subnet, dns;
  return parseIp(value.ip, ip) && parseIp(value.gateway, gateway) && parseIp(value.subnet, subnet) &&
         (!value.dns[0] || parseIp(value.dns, dns));
}

static bool timeZoneValid(const StoredTimeZone& value) {
  return value.magic == TIMEZONE_MAGIC && value.version == TIMEZONE_VERSION &&
         !value.id[sizeof(value.id) - 1] && findTimeZoneById(value.id) &&
         value.crc == timeZoneCrc(value);
}

static const char* currentTimeZone() {
  return timeZoneValid(storedTimeZone) ? storedTimeZone.id : DEFAULT_TIMEZONE_ID;
}

bool hasSavedSettings() { return settingsValid(settings); }
bool stationConnected() {
  return state == STA_CONNECTED && WiFi.status() == WL_CONNECTED && !scheduledApAt && !scheduledRestartAt;
}
bool accessPointMode() { return state == ACCESS_POINT; }
const char* accessPointName() { return apSsid; }
uint32_t reconnectCount() { return reconnects; }

const char* modeName() {
  switch (state) {
    case CONNECTING: return "sta_connecting";
    case STA_CONNECTED: return "sta_connected";
    case ACCESS_POINT: return "ap";
    default: return "idle";
  }
}

static void startAccessPoint() {
  dnsServer.stop();
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, AP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());
  state = ACCESS_POINT;
  stateStartedAt = millis();
  scheduledApAt = 0;
}

static bool configureStation() {
  IPAddress zero(0, 0, 0, 0);
  if (!settings.useStaticIp) return WiFi.config(zero, zero, zero);
  IPAddress ip, gateway, subnet, dns;
  if (!parseIp(settings.ip, ip) || !parseIp(settings.gateway, gateway) || !parseIp(settings.subnet, subnet)) return false;
  if (!parseIp(settings.dns, dns)) dns = gateway;
  return WiFi.config(ip, gateway, subnet, dns);
}

static void startStation() {
  if (!hasSavedSettings()) { startAccessPoint(); return; }
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (!configureStation()) { startAccessPoint(); return; }
  if (settings.password[0]) WiFi.begin(settings.ssid, settings.password);
  else WiFi.begin(settings.ssid);
  state = CONNECTING;
  stateStartedAt = millis();
  scheduledApAt = 0;
}

void begin() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, settings);
  if (!settingsValid(settings)) memset(&settings, 0, sizeof(settings));
  EEPROM.get(sizeof(settings), storedTimeZone);
  if (!timeZoneValid(storedTimeZone)) memset(&storedTimeZone, 0, sizeof(storedTimeZone));
  snprintf(apSsid, sizeof(apSsid), "SmarthomeBridge-%06X", ESP.getChipId());
  WiFi.persistent(false);
  WiFi.hostname("smarthome-bridge");
  state = IDLE;
  if (hasSavedSettings()) startStation(); else startAccessPoint();
}

void process() {
  const uint32_t now = millis();
  if (scheduledRestartAt && static_cast<int32_t>(now - scheduledRestartAt) >= 0) ESP.restart();
  if (scheduledApAt && static_cast<int32_t>(now - scheduledApAt) >= 0) {
    if (transitionAllowed()) startAccessPoint();
    else scheduledApAt = now + MODE_CHANGE_DELAY_MS;
  }
  switch (state) {
    case CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        state = STA_CONNECTED; stateStartedAt = now; ++reconnects;
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        const TimeZoneOption* zone = findTimeZoneById(currentTimeZone());
        configTime(getPosixTimeZone(zone->key), "pool.ntp.org", "time.nist.gov");
      } else if (now - stateStartedAt >= CONNECT_TIMEOUT_MS) {
        if (transitionAllowed()) startAccessPoint();
        else stateStartedAt = now;
      }
      break;
    case STA_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) startStation();
      break;
    case ACCESS_POINT:
      dnsServer.processNextRequest();
      if (hasSavedSettings() && now - stateStartedAt >= AP_RETRY_MS) startStation();
      break;
    default:
      if (hasSavedSettings()) startStation(); else startAccessPoint();
      break;
  }
}

static bool saveSettings(JsonObject wifi, const char* timeZone, String& error) {
  if (!wifi.containsKey("ssid")) { error = "Укажите SSID"; return false; }
  String ssid = wifi["ssid"].as<String>();
  if (!ssid.length() || ssid.length() > 32) { error = "SSID должен содержать от 1 до 32 байт"; return false; }
  String password;
  if (wifi.containsKey("password")) password = wifi["password"].as<String>();
  else if (hasSavedSettings() && ssid == settings.ssid) password = settings.password;
  if (password.length() > 64 || (password.length() && password.length() < 8)) { error = "Пароль WiFi должен быть пустым или содержать 8–64 байта"; return false; }
  const TimeZoneOption* zone = findTimeZoneById(timeZone);
  if (!zone) { error = "Неизвестный часовой пояс"; return false; }
  bool useStatic = wifi["useStaticIp"] | false;
  String ip = wifi["ip"] | "", gateway = wifi["gateway"] | "", subnet = wifi["subnet"] | "", dns = wifi["dns"] | "";
  IPAddress parsed;
  if (useStatic && (!parseIp(ip.c_str(), parsed) || !parseIp(gateway.c_str(), parsed) ||
                    !parseIp(subnet.c_str(), parsed) || (dns.length() && !parseIp(dns.c_str(), parsed)))) {
    error = "Некорректные параметры статического IP"; return false;
  }
  StoredSettings next = {};
  next.magic = SETTINGS_MAGIC; next.version = SETTINGS_VERSION;
  ssid.toCharArray(next.ssid, sizeof(next.ssid)); password.toCharArray(next.password, sizeof(next.password));
  next.useStaticIp = useStatic ? 1 : 0;
  ip.toCharArray(next.ip, sizeof(next.ip)); gateway.toCharArray(next.gateway, sizeof(next.gateway));
  subnet.toCharArray(next.subnet, sizeof(next.subnet)); dns.toCharArray(next.dns, sizeof(next.dns));
  next.crc = settingsCrc(next);
  StoredTimeZone nextTimeZone = {};
  nextTimeZone.magic = TIMEZONE_MAGIC; nextTimeZone.version = TIMEZONE_VERSION;
  strncpy(nextTimeZone.id, zone->id, sizeof(nextTimeZone.id) - 1);
  nextTimeZone.crc = timeZoneCrc(nextTimeZone);
  EEPROM.put(0, next);
  EEPROM.put(sizeof(next), nextTimeZone);
  if (!EEPROM.commit()) { error = "Не удалось записать настройки в EEPROM"; return false; }
  settings = next;
  storedTimeZone = nextTimeZone;
  return true;
}

static void appendStatus(JsonDocument& doc) {
  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = hasSavedSettings() ? settings.ssid : "";
  wifi["passwordConfigured"] = hasSavedSettings() && settings.password[0];
  wifi["useStaticIp"] = hasSavedSettings() && settings.useStaticIp;
  wifi["ip"] = hasSavedSettings() ? settings.ip : "";
  wifi["gateway"] = hasSavedSettings() ? settings.gateway : "";
  wifi["subnet"] = hasSavedSettings() ? settings.subnet : "";
  wifi["dns"] = hasSavedSettings() ? settings.dns : "";
  JsonObject network = doc.createNestedObject("network");
  network["mode"] = modeName(); network["apSsid"] = apSsid;
  network["apPassword"] = AP_PASSWORD;
  network["apIp"] = WiFi.softAPIP().toString(); network["stationIp"] = WiFi.localIP().toString();
  network["saved"] = hasSavedSettings(); network["reconnects"] = reconnects;
  doc["timeZone"] = currentTimeZone();
  const time_t now = time(nullptr);
  doc["epoch"] = static_cast<uint32_t>(now);
  doc["timeValid"] = now >= 1609459200;
}

static void sendConfigPage(AsyncWebServerRequest* request) {
  if (fsAvailable && LittleFS.exists("/www/config.html")) request->send(LittleFS, "/www/config.html", "text/html");
  else request->send_P(200, "text/html; charset=utf-8", FALLBACK_PAGE);
}

static bool collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  if (!total || total > MAX_REQUEST_BODY || index + len > total) {
    if (!index) request->send(413, "application/json", "{\"success\":false,\"message\":\"Request body is too large\"}");
    return false;
  }
  if (!index) {
    request->_tempObject = malloc(total + 1);
    if (!request->_tempObject) { request->send(503, "application/json", "{\"success\":false,\"message\":\"Not enough memory\"}"); return false; }
    request->onDisconnect([request]() {
      if (request->_tempObject) { free(request->_tempObject); request->_tempObject = nullptr; }
    });
  }
  if (!request->_tempObject) return false;
  memcpy(static_cast<uint8_t*>(request->_tempObject) + index, data, len);
  if (index + len != total) return false;
  static_cast<char*>(request->_tempObject)[total] = 0;
  return true;
}

void setupRoutes(AsyncWebServer& server, bool littleFsAvailable, bool (*transitionGuard)()) {
  fsAvailable = littleFsAvailable;
  allowTransition = transitionGuard;
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (accessPointMode()) { request->redirect("/config"); return; }
    if (fsAvailable && LittleFS.exists("/www/log.html")) request->send(LittleFS, "/www/log.html", "text/html");
    else request->send(503, "text/plain", "UI unavailable");
  });
  server.on("/config", HTTP_GET, sendConfigPage);
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) { request->redirect("/config"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) { request->redirect("/config"); });
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) { request->redirect("/config"); });
  server.on("/api/network/config", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(1024); appendStatus(doc); String response; serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  server.on("/api/timezones", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(2048);
    JsonArray zones = doc.createNestedArray("timeZones");
    for (size_t i = 0; i < TIME_ZONES_COUNT; ++i) {
      JsonObject zone = zones.createNestedObject();
      zone["id"] = TIME_ZONES[i].id;
      zone["label"] = TIME_ZONES[i].label;
    }
    String response; serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  AsyncCallbackWebHandler* save = new AsyncCallbackWebHandler();
  save->setUri("/api/network/config"); save->setMethod(HTTP_POST);
  save->onRequest([](AsyncWebServerRequest*) {});
  save->onBody([](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!collectBody(request, data, len, index, total) || index + len != total) return;
    if (!transitionAllowed()) {
      free(request->_tempObject); request->_tempObject = nullptr;
      request->send(409, "application/json", "{\"success\":false,\"message\":\"Bridge is busy\"}"); return;
    }
    DynamicJsonDocument doc(1024);
    DeserializationError parseError = deserializeJson(doc, static_cast<char*>(request->_tempObject));
    free(request->_tempObject); request->_tempObject = nullptr;
    if (parseError || !doc["wifi"].is<JsonObject>()) { request->send(400, "application/json", "{\"success\":false,\"message\":\"Некорректный JSON или отсутствует блок wifi\"}"); return; }
    String error;
    const char* timeZone = doc["timeZone"] | DEFAULT_TIMEZONE_ID;
    if (!saveSettings(doc["wifi"].as<JsonObject>(), timeZone, error)) {
      DynamicJsonDocument responseDoc(256); responseDoc["success"] = false; responseDoc["message"] = error;
      String response; serializeJson(responseDoc, response); request->send(400, "application/json", response); return;
    }
    scheduledRestartAt = millis() + RESTART_DELAY_MS;
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Настройки сохранены, мост перезагрузится\"}");
  });
  server.addHandler(save);
  server.on("/api/network/ap", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (scheduledRestartAt) { request->send(409, "application/json", "{\"success\":false,\"message\":\"Restart is already scheduled\"}"); return; }
    if (!transitionAllowed()) { request->send(409, "application/json", "{\"success\":false,\"message\":\"Bridge is busy\"}"); return; }
    scheduledApAt = millis() + MODE_CHANGE_DELAY_MS;
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Точка доступа запускается\"}");
  });
}

}
