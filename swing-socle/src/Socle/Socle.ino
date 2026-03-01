/**
 * Use 2.7.4 esp8266 core
 * lwip v2 Higher bandwidth; CPU 80 MHz
 * 1M (none) !!! ->  not enough flash for 3.0+
 * 
 * dependencies:
 * ESPAsyncWebServer https://github.com/me-no-dev/ESPAsyncWebServer
 * ESPMiio https://github.com/ar2rus/ESPMiIO
 * ESPInputs https://github.com/ar2rus/ESPInputs
 * PTTasker https://github.com/ar2rus/PTTasker
 * ClunetMulticast https://github.com/ar2rus/ClunetMulticast
 */

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
//#include <TZ.h>
#include <time.h>

#include <Servo.h>

#include <DNSServer.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>
#include <ClunetMulticast.h>

#include <ESPInputs.h>
#include <ESPMiio.h>
#include <ESPVacuum.h>

#include <ptt.h>

#include "Socle.h"

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
  char mirobo_ip[16];
  char mirobo_token[65];
};

AsyncWebServer server(HTTP_PORT);
DNSServer dns_server;

Servo servo;

PTTasker tasker;
Inputs inputs;

IPAddress mirobo_ip;
char mirobo_ip_text[16] = "";
char mirobo_token[65] = "";
MiioDevice *mirobo = NULL;

int mirobo_state = VS_UNKNOWN;

ClunetMulticast clunet(CLUNET_ID, CLUNET_DEVICE);

static const char SOCLE_UI_HTML[] PROGMEM = R"HTML(
<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>Swing Socle</title><style>body{font:14px Arial,sans-serif;background:#f5f5f5;color:#222;margin:0;padding:12px}.c{background:#fff;max-width:760px;margin:0 auto 12px;padding:14px;border-radius:12px;box-shadow:0 4px 16px rgba(0,0,0,.08)}h1,h2{margin:0 0 12px}.a a{display:inline-block;margin:0 8px 8px 0;padding:10px 12px;background:#1f6feb;color:#fff;text-decoration:none;border-radius:8px}.a .t{background:#0f766e}.a .d{background:#d97706}.m{color:#555}.o,.e{padding:10px;border-radius:8px;margin:0 0 12px}.o{background:#ecfdf3;color:#166534}.e{background:#fef2f2;color:#991b1b}input{display:block;width:100%;box-sizing:border-box;padding:10px;margin:6px 0 10px;border:1px solid #c9c9c9;border-radius:8px}button{padding:10px 12px;background:#111827;color:#fff;border:0;border-radius:8px}.k{display:flex;align-items:center;gap:8px;margin:10px 0}.k input{width:auto;margin:0}</style><div class=c><h1>Swing Socle</h1>%M%<p><b>Door:</b> %D%<br><b>Angle:</b> %A%<br><b>Mirobo:</b> %R%<br><b>WiFi:</b> %W%</p><div class=a><a href="/up?ui=1">Open</a><a class=d href="/down?ui=1">Close</a><a class=t href="/toggle?ui=1">Toggle</a><a href="/mirobo_start?ui=1">Mirobo</a></div></div><div class=c><h2>Network</h2><p class=m>%N%</p><form method=POST action=/wifi><label for=ssid>SSID</label><input id=ssid name=ssid maxlength=32 value="%S%"><label for=password>Password</label><input id=password name=password type=password maxlength=64 value="%P%"><label class=k for=use_static_ip><input id=use_static_ip name=use_static_ip type=checkbox value=1 %C%><span>Static IP</span></label><label for=ip>IP</label><input id=ip name=ip inputmode=decimal maxlength=15 placeholder="192.168.1.125" value="%I%"><label for=gateway>Gateway</label><input id=gateway name=gateway inputmode=decimal maxlength=15 placeholder="192.168.1.1" value="%G%"><label for=subnet>Subnet</label><input id=subnet name=subnet inputmode=decimal maxlength=15 placeholder="255.255.255.0" value="%U%"><label for=dns>DNS</label><input id=dns name=dns inputmode=decimal maxlength=15 placeholder="192.168.1.1" value="%X%"><button type=submit>Save WiFi and reboot</button></form></div><div class=c><h2>Mirobo</h2><p class=m>Robot vacuum settings used for status polling and start/stop commands.</p><form method=POST action=/mirobo><label for=mirobo_ip>Mirobo IP</label><input id=mirobo_ip name=mirobo_ip inputmode=decimal maxlength=15 placeholder="192.168.1.27" value="%Y%"><label for=mirobo_token>Mirobo token</label><input id=mirobo_token name=mirobo_token maxlength=64 value="%T%"><button type=submit>Save Mirobo and reboot</button></form></div>
)HTML";

WifiSettings wifi_settings = {};
WifiState wifi_state = WIFI_STATE_IDLE;

bool server_started = false;
bool ota_ready = false;
bool clunet_ready = false;
bool clunet_handlers_registered = false;

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
bool saveMiroboSettings(const String& ip, const String& token);
bool hasSavedWiFiSettings();
bool parseIPAddress(const char* value, IPAddress& address);
bool isPrintableValue(const char* value);
bool configureStationNetwork();
void setupMiroboDevice();

void setupWebServer();
void setupClunetCallbacks();
void updateWiFiState();
void startStationConnection();
void handleStationConnected();
void startAccessPoint();
void ensureClunetConnected();
void scheduleReboot();
void sendActionResponse(AsyncWebServerRequest *request, bool ok);
void sendJsonResult(AsyncWebServerRequest *request, const char* key, bool value);

String htmlEscape(const String& value);
String wifiStateLabel();
String networkHint();
String ipAddressValue(const char* value);
String doorStateLabel();
String miroboStateLabel();
void sendUiPage(AsyncWebServerRequest *request, const String& message = "", bool is_error = false, int status_code = 200);

void server_response(AsyncWebServerRequest *request, unsigned int response);
void send_clunet_device_state_info(uint8_t address);
void door_info(uint8_t address);
void mirobo_toggle();
bool hasSavedMiroboSettings();

void servo_attach(){
  servo.attach(SERVO_PIN);
  digitalWrite(LED_BLUE_PORT, HIGH);
}

void servo_detach(){
  servo.detach();
  digitalWrite(LED_BLUE_PORT, LOW);
}

void stop_servo_group(){
  tasker.stopAll(TASKER_GROUP_SERVO);
  tone(BUZZER_PORT, 0);
}

PT_THREAD(beep(pt_t *p, int freq, int tone_delay, int pause_delay)){
  PT_BEGIN(p);
  tone(BUZZER_PORT, freq);
  PT_DELAY(p, tone_delay);
  tone(BUZZER_PORT, 0);
  PT_DELAY(p, pause_delay);
  PT_END(p);
}

PT_THREAD(beep_warning(pt_t *p)){
  PT_BEGIN(p);
  PT_SUBTHREAD_R(p, beep, BEEP_WARNING_REPEATS, BEEP_WARNING_FREQ, BEEP_WARNING_DELAY, BEEP_WARNING_DELAY);
  PT_END(p);
}

PT_THREAD(beep_error(pt_t *p)){
  PT_BEGIN(p);
  PT_SUBTHREAD(p, beep, BEEP_ERROR_FREQ, BEEP_ERROR_DELAY, 0);
  PT_END(p);
}

PT_THREAD(beep_info(pt_t *p)){
  PT_BEGIN(p);
  PT_SUBTHREAD_R(p, beep, 1, BEEP_INFO_FREQ, BEEP_INFO_DELAY, 0);
  PT_END(p);
}

void beep_error(){
  tasker.once(&beep_error);
}

PT_THREAD(beep_app_start(pt_t *p)){
  PT_BEGIN(p);
  PT_SUBTHREAD(p, beep, BEEP_APP_START_FREQ, BEEP_APP_START_DELAY, 0);
  PT_END(p);
}

void stop_group_led_red(){
  tasker.stopAll(TASKER_GROUP_LED_RED);
  digitalWrite(LED_RED_PORT, LOW);
}

PT_THREAD(led_red_blink(pt_t *p, int delay_high, int delay_low)){
  PT_BEGIN(p);
  digitalWrite(LED_RED_PORT, HIGH);
  PT_DELAY(p, delay_high);
  digitalWrite(LED_RED_PORT, LOW);
  PT_DELAY(p, delay_low);
  PT_END(p);
}

PT_THREAD(led_red_ok(pt_t *p)){
  PT_BEGIN(p);
  PT_SUBTHREAD(p, led_red_blink, 100, 900);
  PT_END(p);
}

void led_red_ok(){
  stop_group_led_red();
  tasker.loop(TASKER_GROUP_LED_RED, &led_red_ok);
}

PT_THREAD(led_red_error_1(pt_t *p)){
  PT_BEGIN(p);
  PT_SUBTHREAD(p, led_red_blink, 250, 250);
  PT_END(p);
}

void led_red_error_1(){
  stop_group_led_red();
  tasker.loop(TASKER_GROUP_LED_RED, &led_red_error_1);
}

PT_THREAD(led_red_error_2(pt_t *p)){
  PT_BEGIN(p);
  PT_SUBTHREAD(p, led_red_blink, 500, 500);
  PT_END(p);
}

void led_red_error_2(){
  stop_group_led_red();
  tasker.loop(TASKER_GROUP_LED_RED, &led_red_error_2);
}

PT_THREAD(servo_angle(pt_t *p, int angle, int step_delay)){
  int val = servo.read();
  PT_BEGIN(p);
  servo.write(val);
  servo_attach();
  while (val != angle){
    PT_DELAY(p, step_delay);
    val += (angle > val ? 1 : -1);
    servo.write(val);
    //val = servo.read();
  }
  if (clunet_ready) {
    clunet.send(CLUNET_ADDRESS_BROADCAST, CLUNET_COMMAND_SERVO_INFO, (char*)&angle, 2);
  }
  PT_END(p);
}

PT_THREAD(servo_down(pt_t *p)){
  PT_BEGIN(p);
  //если не в нижнем положении
  if (servo.read() != SERVO_DOWN_ANGLE){
    //предупреждение
    PT_SUBTHREAD(p, beep_warning);
    //опускаем
    PT_SUBTHREAD(p, servo_angle, SERVO_DOWN_ANGLE, SERVO_STEP_DOWN_DELAY);
    PT_DELAY(p, SERVO_DOWN_TIMEOUT_DEFAULT);
  }
  //выключаем серву
  servo_detach();
  PT_END(p);
}

bool servo_down(bool force){
  if (force){
    stop_servo_group();
  }
  return tasker.once(TASKER_GROUP_SERVO, &servo_down);
}

uint32_t servo_up_timeout;

PT_THREAD(servo_up(pt_t *p)){
  PT_BEGIN(p);
  //если не в верхнем положении
  if (servo.read() != SERVO_UP_ANGLE){
    //предупреждение
    PT_SUBTHREAD(p, beep_warning);
    //поднимаем
    PT_SUBTHREAD(p, servo_angle, SERVO_UP_ANGLE, SERVO_STEP_UP_DELAY);
  }
  //в верхнем положении находимся не более timeout
  PT_WAIT(p, servo.read() != SERVO_UP_ANGLE, servo_up_timeout);
  //если все еще в верхнем положении
  if (servo.read() == SERVO_UP_ANGLE){
     //опускаем вниз
     PT_SUBTHREAD(p, servo_down);
  }
  PT_END(p);
}

bool servo_up(bool force, uint32_t timeout = SERVO_UP_TIMEOUT_DEFAULT){
  if (force){
    stop_servo_group();
  }
  servo_up_timeout = timeout;
  return tasker.once(TASKER_GROUP_SERVO, &servo_up);
}

bool servo_toggle(bool force){
  //если куда-то двигаемся(не в крайних положениях), то переключиться нельзя
  switch (servo.read()){
    case SERVO_UP_ANGLE:
      return servo_down(force);
    case SERVO_DOWN_ANGLE:
      return servo_up(force);
    default:
      return false;
  }
}

void door_info(uint8_t address){
  if (!clunet_ready) {
    return;
  }

  char door_state = servo.read() == SERVO_UP_ANGLE;
  clunet.send(address, CLUNET_COMMAND_DOOR_INFO, &door_state, 1);
}

PT_THREAD(mirobo_connect(pt_t *p)){
  PT_BEGIN(p);
  if (wifi_state == WIFI_STATE_STA_CONNECTED) {
    if (mirobo && mirobo_token[0]) {
      if (!mirobo->isConnected()){
        mirobo->connect([](MiioError e){
          led_red_error_1();
          Serial.printf("PT connecting error: %d\n", e);
        });
      }

      PT_WAIT_UNTIL(p, !mirobo->isBusy());
      if (mirobo->isConnected()){
        led_red_ok();
      }
    }
  }
  PT_END(p);
}

PT_THREAD(mirobo_toggle(pt_t *p)){
  PT_BEGIN(p);
  PT_SUBTHREAD(p, beep_app_start);
  PT_SUBTHREAD(p, mirobo_connect);

  if (mirobo && mirobo->isConnected()){
    if (mirobo_state == VS_CHARGING){
      mirobo->send("app_start", NULL, [](MiioError e){
        beep_error();
        Serial.printf("PT app_start error: %d\n", e);
      });
    } else {
      mirobo->send("app_pause", NULL, [](MiioError e){
        beep_error();
        Serial.printf("PT app_pause error: %d\n", e);
      });
      PT_WAIT_UNTIL(p, !mirobo->isBusy());
      mirobo->send("app_charge", NULL, [](MiioError e){
        beep_error();
        Serial.printf("PT app_charge error: %d\n", e);
      });
    }
  } else {
    beep_error();
    Serial.printf("PT error: mirobo not connected");
  }
  PT_END(p);
}

void mirobo_toggle(){
  tasker.once(TASKER_GROUP_MIIO, &mirobo_toggle);
}

PT_THREAD(check_mirobo_status(pt_t *p)){
  PT_BEGIN(p);

  if (wifi_state != WIFI_STATE_STA_CONNECTED) {
    if (mirobo && mirobo->isConnected()) {
      mirobo->disconnect();
    }
    PT_DELAY(p, MIROBO_CHECK_STATUS_PERIOD);
  } else {
    PT_SUBTHREAD(p, mirobo_connect);

    if (!mirobo || !mirobo->send("get_status", [](MiioResponse response){
          if (!response.getResult().isNull()){
            JsonVariant state = response.getResult()["state"];
            if (!state.isNull()){
              led_red_ok();
              int s = state.as<int>();
              Serial.printf("state=%d\n", s);
              if (s != mirobo_state){
                switch (s){
                  case VS_GOING_HOME:
                    servo_up(true, MIROBO_MAX_GOING_HOME_PERIOD);
                    break;
                  case VS_CHARGING:
                    servo_down(true);
                    break;
                  default:
                    if (mirobo_state == VS_CHARGING){
                      switch (s){
                        case VS_CLEANING:
                        case VS_MANUAL:
                        case VS_SPOT_CLEANUP:
                        case VS_GOING_TO_TARGET:
                        case VS_CLEANING_ZONE:
                          servo_up(true, 45000);
                          break;
                      }
                    } else if (mirobo_state == VS_GOING_HOME){
                      switch (s){
                        case VS_REMOTE_CONTROL:
                        case VS_CLEANING:
                        case VS_MANUAL:
                        case VS_SPOT_CLEANUP:
                        case VS_SHUTDOWN:
                        case VS_GOING_TO_TARGET:
                        case VS_CLEANING_ZONE:
                        case VS_PAUSED:
                          servo_down(true);
                          break;
                      }
                    }
                }
                mirobo_state = s;
                send_clunet_device_state_info(CLUNET_ADDRESS_BROADCAST);
              }
            }
          } else if (!response.getError().isNull()){
            led_red_error_2();
            if (mirobo) {
              mirobo->disconnect();
            }
            Serial.println("PT response error");
          }
        }, [](MiioError e){
          led_red_error_2();
          if (mirobo) {
            mirobo->disconnect();
          }
          Serial.printf("PT get_status error: %d\n", e);
        }
    )){
      led_red_error_2();
      if (mirobo) {
        mirobo->disconnect();
      }
      Serial.printf("PT send get_status error");
    }
    PT_DELAY(p, MIROBO_CHECK_STATUS_PERIOD);
  }
  PT_END(p);
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
  wifi_settings.mirobo_ip[sizeof(wifi_settings.mirobo_ip) - 1] = '\0';
  wifi_settings.mirobo_token[sizeof(wifi_settings.mirobo_token) - 1] = '\0';
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

  if (!parseIPAddress(wifi_settings.mirobo_ip, mirobo_ip)) {
    memset(wifi_settings.mirobo_ip, 0, sizeof(wifi_settings.mirobo_ip));
  }

  if (!isPrintableValue(wifi_settings.mirobo_token)) {
    memset(wifi_settings.mirobo_token, 0, sizeof(wifi_settings.mirobo_token));
  }

  return wifi_settings.ssid[0];
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
  char mirobo_ip_saved[sizeof(wifi_settings.mirobo_ip)];
  char mirobo_token_saved[sizeof(wifi_settings.mirobo_token)];

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

  memcpy(mirobo_ip_saved, wifi_settings.mirobo_ip, sizeof(mirobo_ip_saved));
  memcpy(mirobo_token_saved, wifi_settings.mirobo_token, sizeof(mirobo_token_saved));
  memset(&wifi_settings, 0, sizeof(wifi_settings));
  wifi_settings.magic = WIFI_SETTINGS_MAGIC;
  ssid.toCharArray(wifi_settings.ssid, sizeof(wifi_settings.ssid));
  password.toCharArray(wifi_settings.password, sizeof(wifi_settings.password));
  wifi_settings.use_static_ip = use_static_ip ? 1 : 0;
  ip.toCharArray(wifi_settings.ip, sizeof(wifi_settings.ip));
  gateway.toCharArray(wifi_settings.gateway, sizeof(wifi_settings.gateway));
  subnet.toCharArray(wifi_settings.subnet, sizeof(wifi_settings.subnet));
  dns.toCharArray(wifi_settings.dns, sizeof(wifi_settings.dns));
  memcpy(wifi_settings.mirobo_ip, mirobo_ip_saved, sizeof(wifi_settings.mirobo_ip));
  memcpy(wifi_settings.mirobo_token, mirobo_token_saved, sizeof(wifi_settings.mirobo_token));

  EEPROM.put(0, wifi_settings);
  return EEPROM.commit();
}

bool saveMiroboSettings(const String& ip, const String& token) {
  IPAddress parsed_ip;

  if (
    !ip.length() ||
    ip.length() >= sizeof(wifi_settings.mirobo_ip) ||
    !parsed_ip.fromString(ip) ||
    !token.length() ||
    token.length() >= sizeof(wifi_settings.mirobo_token)
  ) {
    return false;
  }

  if (wifi_settings.magic != WIFI_SETTINGS_MAGIC) {
    wifi_settings.magic = WIFI_SETTINGS_MAGIC;
  }

  ip.toCharArray(wifi_settings.mirobo_ip, sizeof(wifi_settings.mirobo_ip));
  token.toCharArray(wifi_settings.mirobo_token, sizeof(wifi_settings.mirobo_token));

  EEPROM.put(0, wifi_settings);
  return EEPROM.commit();
}

bool hasSavedWiFiSettings() {
  return wifi_settings.magic == WIFI_SETTINGS_MAGIC && wifi_settings.ssid[0];
}

bool hasSavedMiroboSettings() {
  IPAddress address;
  return parseIPAddress(wifi_settings.mirobo_ip, address) && isPrintableValue(wifi_settings.mirobo_token);
}

bool parseIPAddress(const char* value, IPAddress& address) {
  return value[0] && address.fromString(value);
}

bool isPrintableValue(const char* value) {
  if (!value[0]) {
    return false;
  }

  for (size_t i = 0; value[i]; i++) {
    if (value[i] < 32 || value[i] > 126) {
      return false;
    }
  }

  return true;
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

void sendJsonResult(AsyncWebServerRequest *request, const char* key, bool value) {
  char temp[24];
  snprintf(temp, sizeof(temp), "{\"%s\": %u}", key, value ? 1 : 0);
  request->send(200, "application/json", temp);
}

void setupMiroboDevice() {
  if (mirobo) {
    mirobo->disconnect();
    delete mirobo;
    mirobo = NULL;
  }

  if (!hasSavedMiroboSettings()) {
    mirobo_ip = IPAddress();
    mirobo_ip_text[0] = '\0';
    mirobo_token[0] = '\0';
    memset(wifi_settings.mirobo_ip, 0, sizeof(wifi_settings.mirobo_ip));
    memset(wifi_settings.mirobo_token, 0, sizeof(wifi_settings.mirobo_token));
    return;
  }

  strlcpy(mirobo_ip_text, wifi_settings.mirobo_ip, sizeof(mirobo_ip_text));
  strlcpy(mirobo_token, wifi_settings.mirobo_token, sizeof(mirobo_token));
  parseIPAddress(wifi_settings.mirobo_ip, mirobo_ip);
  mirobo = new MiioDevice(&mirobo_ip, mirobo_token, 2000);
}

void setupWebServer() {
  if (server_started) {
    return;
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendUiPage(request);
  });

  server.on("/ui", HTTP_GET, [](AsyncWebServerRequest *request) {
    String message = "";
    bool is_error = false;

    if (request->hasArg("saved")) {
      message = "Settings saved. Device will reboot.";
    } else if (request->hasArg("message") && request->arg("message") == "action_failed") {
      message = "Action failed.";
      is_error = true;
    }

    sendUiPage(request, message, is_error);
  });

  server.on("/up", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool ui_request = request->hasArg("ui");
    bool ok = false;

    if (request->args() == 0 || (ui_request && request->args() == 1)) {
      ok = servo_up(false);
    }

    if (ui_request) {
      sendActionResponse(request, ok);
      return;
    }

    sendJsonResult(request, "result", ok);
  });

  server.on("/down", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool ui_request = request->hasArg("ui");
    bool ok = false;

    if (request->args() == 0 || (ui_request && request->args() == 1)) {
      ok = servo_down(false);
    }

    if (ui_request) {
      sendActionResponse(request, ok);
      return;
    }

    sendJsonResult(request, "result", ok);
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool ui_request = request->hasArg("ui");
    bool ok = false;

    if (request->args() == 0 || (ui_request && request->args() == 1)) {
      ok = servo_toggle(false);
    }

    if (ui_request) {
      sendActionResponse(request, ok);
      return;
    }

    sendJsonResult(request, "result", ok);
  });

  server.on("/mirobo_state", HTTP_GET, [](AsyncWebServerRequest *request) {
    char temp[24];
    snprintf(temp, sizeof(temp), "{\"state\": %d}", mirobo_state);
    request->send(200, "application/json", temp);
  });

  server.on("/mirobo_start", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool ui_request = request->hasArg("ui");
    bool ok = wifi_state == WIFI_STATE_STA_CONNECTED && mirobo && mirobo_token[0];

    if (ok) {
      mirobo_toggle();
    }

    if (ui_request) {
      sendActionResponse(request, ok);
      return;
    }

    server_response(request, ok ? 200 : 404);
  });

//   server.on("/hello", HTTP_GET, [](AsyncWebServerRequest * request) {
//      if (mirobo->connect([](MiioError e){
//          Serial.printf("hello error: %d", e);
//        })){
//        request->send(200);
//        return;
//      }
//
//       request->send(500);
//   });
//
//   server.on("/status", HTTP_GET, [](AsyncWebServerRequest * request) {
//      if (mirobo->isConnected()){
//        if (mirobo->send("get_status", 
//        [](Response response){
//          Serial.println("MIROBO status received");
//        }, 
//        [](MiioError e){
//          Serial.printf("MIROBO get_status error: %d\n", e);
//        })){
//          request->send(200);
//          return;
//        }
//      }else{
//        request->send(403);
//      }
//      
//      if (mirobo->send("get_status", [](Response response){
//          Serial.println("MIROBO response received");
//        })){
//        request->send(200);
//        return;
//      }
//       request->send(500);
//   });

  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    String message = "";
    bool is_error = false;

    if (request->hasArg("saved")) {
      message = "Settings saved. Device will reboot.";
    } else if (request->hasArg("message") && request->arg("message") == "action_failed") {
      message = "Action failed.";
      is_error = true;
    }

    sendUiPage(request, message, is_error);
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

    ssid.trim();
    password.trim();
    ip.trim();
    gateway.trim();
    subnet.trim();
    dns.trim();

    if (!saveWiFiSettings(ssid, password, use_static_ip, ip, gateway, subnet, dns)) {
      sendUiPage(request, "Unable to save WiFi settings.", true, 400);
      return;
    }

    scheduleReboot();
    sendUiPage(request, "Settings saved. Device will reboot.");
  });

  server.on("/mirobo", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasArg("mirobo_ip") || !request->hasArg("mirobo_token")) {
      request->send(400, "text/plain", "Mirobo IP and token are required\n");
      return;
    }

    String ip = request->arg("mirobo_ip");
    String token = request->arg("mirobo_token");
    ip.trim();
    token.trim();

    if (!saveMiroboSettings(ip, token)) {
      sendUiPage(request, "Unable to save Mirobo settings.", true, 400);
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
    Serial.printf("message received: %d\n", packet->command);
    switch (packet->command) {
      case CLUNET_COMMAND_DEVICE_STATE:
        if (packet->size == 1) {
          switch (packet->data[0]) {
            case 0x01:
              mirobo_toggle();
              break;
            case 0xFF:
              send_clunet_device_state_info(packet->src);
              break;
          }
        }
        break;
      case CLUNET_COMMAND_BEEP:
        tasker.once(&beep_info);
        break;
      //mirobo_toggle by long press on kitchen button
      case CLUNET_COMMAND_BUTTON_INFO:
        if (packet->src == 0x1D){ //kitchen
          if (packet->size == 2 && packet->data[0] == 0x02 && packet->data[1] == 0x01) {
            mirobo_toggle();
          }
        }
        break;
      case CLUNET_COMMAND_RC_BUTTON_PRESSED:
        if (packet->src == 0x1D){ //kitchen
          if (packet->size == 3 && packet->data[0] == 0x00 && packet->data[1] == 0x00) {
            switch (packet->data[2]) {
              case 0x08:
                servo_toggle(false);
                break;
              case 0x50:
                mirobo_toggle();
                break;
            }
          }
        }
        break;
      case CLUNET_COMMAND_DOOR:
        if (packet->size == 0) {
          door_info(packet->src);
        } else if (packet->size == 1) {
          switch (packet->data[0]) {
            case 0x00:
              servo_down(false);
              break;
            case 0x01:
              servo_up(false);
              break;
            case 0x02:
              servo_toggle(false);
              break;
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
  if (mirobo) {
    mirobo->disconnect();
  }
}

void handleStationConnected() {
  if (wifi_state == WIFI_STATE_STA_CONNECTED) {
    return;
  }

  wifi_state = WIFI_STATE_STA_CONNECTED;
  wifi_connect_started_at = 0;

  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

//  configTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  configTime((int)(4 * 3600), 0, "pool.ntp.org", "time.nist.gov", NULL);

  if (!ota_ready) {
    ArduinoOTA.begin();
    ota_ready = true;
  }

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
  if (mirobo) {
    mirobo->disconnect();
  }
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
    send_clunet_device_state_info(CLUNET_ADDRESS_BROADCAST);
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
        clunet_ready = false;
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

void sendActionResponse(AsyncWebServerRequest *request, bool ok) {
  String location = "/ui";
  if (!ok) {
    location += "?message=action_failed";
  }
  request->redirect(location);
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

String doorStateLabel() {
  switch (servo.read()) {
    case SERVO_UP_ANGLE:
      return F("Open");
    case SERVO_DOWN_ANGLE:
      return F("Closed");
    default:
      return F("Moving");
  }
}

String miroboStateLabel() {
  switch (mirobo_state) {
    case VS_CHARGING:
      return F("Charging");
    case VS_CLEANING:
      return F("Cleaning");
    case VS_GOING_HOME:
      return F("Going home");
    case VS_PAUSED:
      return F("Paused");
    case VS_MANUAL:
      return F("Manual");
    case VS_SPOT_CLEANUP:
      return F("Spot cleanup");
    case VS_GOING_TO_TARGET:
      return F("Going to target");
    case VS_CLEANING_ZONE:
      return F("Cleaning zone");
    case VS_REMOTE_CONTROL:
      return F("Remote control");
    case VS_SHUTDOWN:
      return F("Shutdown");
    default:
      return String(F("Unknown (")) + mirobo_state + ')';
  }
}

void sendUiPage(AsyncWebServerRequest *request, const String& message, bool is_error, int status_code) {
  String page = FPSTR(SOCLE_UI_HTML);
  String message_block = "";

  if (message.length()) {
    message_block = is_error ? F("<div class='error'>") : F("<div class='ok'>");
    message_block += htmlEscape(message);
    message_block += F("</div>");
  }

  page.replace("%M%", message_block);
  page.replace("%D%", doorStateLabel());
  page.replace("%A%", String(servo.read()));
  page.replace("%R%", miroboStateLabel());
  page.replace("%W%", wifiStateLabel());
  page.replace("%N%", networkHint());
  page.replace("%S%", htmlEscape(String(wifi_settings.ssid)));
  page.replace("%P%", htmlEscape(String(wifi_settings.password)));
  page.replace("%C%", wifi_settings.use_static_ip ? String(F("checked")) : String(""));
  page.replace("%I%", ipAddressValue(wifi_settings.ip));
  page.replace("%G%", ipAddressValue(wifi_settings.gateway));
  page.replace("%U%", ipAddressValue(wifi_settings.subnet));
  page.replace("%X%", ipAddressValue(wifi_settings.dns));
  page.replace("%Y%", ipAddressValue(wifi_settings.mirobo_ip));
  page.replace("%T%", htmlEscape(String(wifi_settings.mirobo_token)));

  request->send(status_code, "text/html; charset=utf-8", page);
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

void send_clunet_device_state_info(uint8_t address){
  if (!clunet_ready) {
    return;
  }

  char data[] = {3, *(char*)&mirobo_state};
  clunet.send(address, CLUNET_COMMAND_DEVICE_STATE_INFO, data, sizeof(data));
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  pinMode(LED_RED_PORT, OUTPUT);
  pinMode(LED_BLUE_PORT, OUTPUT);
  pinMode(BUZZER_PORT, OUTPUT);

  servo.write(SERVO_DOWN_ANGLE);
  servo_detach();

  uint16_t id_t0 = inputs.on(0, STATE_CHANGE, 10, [](uint8_t state){
    Serial.println("task0");
    digitalWrite(LED_RED_PORT, !state);
  });

  inputs.on(0, STATE_LOW, 5000, [id_t0](uint8_t state){
    Serial.println("removing task");
    inputs.remove(id_t0);
  });

  EEPROM.begin(sizeof(WifiSettings));
  loadWiFiSettings();
  setupMiroboDevice();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname("swing-socle");

  snprintf(access_point_ssid, sizeof(access_point_ssid), "%s-%06X", WIFI_AP_SSID_PREFIX, ESP.getChipId());

  ArduinoOTA.setHostname("swing-socle");
  ArduinoOTA.onStart([]() {
    Serial.println("OTA started");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA finished");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\nError[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("OTA auth failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("OTA begin failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("OTA connect failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("OTA receive failed");
    else if (error == OTA_END_ERROR) Serial.println("OTA end failed");
  });

  setupWebServer();
  tasker.loop(TASKER_GROUP_MIIO, &check_mirobo_status);

  if (hasSavedWiFiSettings()) {
    startStationConnection();
  } else {
    Serial.println("WiFi credentials not found, starting AP mode");
    startAccessPoint();
  }
}

void loop() {
  tasker.handle();
  inputs.handle();

  updateWiFiState();

  if (wifi_state == WIFI_STATE_STA_CONNECTED && ota_ready) {
    ArduinoOTA.handle();
  }

  if (reboot_scheduled_at && (long)(millis() - reboot_scheduled_at) >= 0) {
    ESP.restart();
  }

  yield();
}
