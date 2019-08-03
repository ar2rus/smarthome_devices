/**
 * Use 2.5.2 esp8266 core
 * lwip 1.4 Higher bandwidth; CPU 80 MHz
 * 1M (128K)
 * 
 * dependencies:
 * IRremoteESP8266 2.6.4
 * Radio by Matthias Hertel
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "Audiobox.h"
#include "lc75341.h"

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

#include "ESPAsyncTCP.h"
#include "ESPAsyncWebServer.h"

#include "ClunetMulticast.h"
#include "ClunetMulticastConfig.h"
#include "Credentials.h"

#include <EEPROM.h>

const char *ssid = AP_SSID;
const char *pass = AP_PASSWORD;

IPAddress ip(192, 168, 1, 124); //Node static IP
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer server(80);

lc75341 audio(14, 16, 12);

IRrecv irrecv(13);


decode_results ir_results;
char nec_data[3];

uint32_t delayedResponseCounterValue = 0;


#include <Wire.h>

#include <radio.h>
#include <RDA5807M.h>

#include <RDSParser.h>


RDA5807M radio;    // Create an instance of Class for RDA5807M Chip
RDSParser rds;



int8_t num_channels = -1;  //cache
int8_t cur_channel = -1;

uint8_t FM_set_num_channels(uint8_t num){
  if (num <= FM_MAX_NUM_CHANNELS){
    num_channels = num; 
    if (cur_channel >= num_channels){
      cur_channel = -1;
    }

    EEPROM.write(FM_PROGRAMS_EEPROM_OFFSET, num_channels);
    EEPROM.commit();
    return 1;
  }
  return 0;
}

uint8_t FM_clear_channels(){
  return FM_set_num_channels(0);
}

int8_t FM_get_num_channels(){
  if (num_channels < 0){
    num_channels = EEPROM.read(FM_PROGRAMS_EEPROM_OFFSET);
  }
  
  return num_channels;
}

int8_t FM_save_channel(uint8_t num_channel, uint16_t frequency){
  if (num_channel < FM_MAX_NUM_CHANNELS && num_channel <= FM_get_num_channels() && num_channel>=0){
    EEPROM.write(FM_PROGRAMS_EEPROM_OFFSET + num_channel*2 + 1, (frequency >> 8) & 0xFF);
    EEPROM.write(FM_PROGRAMS_EEPROM_OFFSET + num_channel*2 + 2, frequency & 0xFF);

    if (FM_get_num_channels() == num_channel){
      FM_set_num_channels(num_channel + 1);
    }
    return num_channel;
  }
  return -1;
}

int8_t FM_add_channel(uint16_t frequency){
  return FM_save_channel(FM_get_num_channels(), frequency);
}

uint16_t FM_get_channel_frequency(uint8_t num_channel){
  if (num_channel < FM_get_num_channels() && num_channel >= 0){
    uint16_t v = 0;
    v |= EEPROM.read(FM_PROGRAMS_EEPROM_OFFSET + num_channel*2 + 1) << 8;
    v |= EEPROM.read(FM_PROGRAMS_EEPROM_OFFSET + num_channel*2 + 2);
    return v;
  }
  return 0;
}

uint8_t FM_select_channel(uint8_t num_channel){
  uint16_t freq = FM_get_channel_frequency(num_channel);
  if (freq){
    cur_channel = num_channel;
    radio.setFrequency(freq);

    return 1;
  }
  return 0;
}

uint8_t FM_select_next_channel(uint8_t up){
  if (FM_get_num_channels() > 0){
    if (up){
      cur_channel++;
    }else{
      cur_channel--;
    }
    
    if (cur_channel >= FM_get_num_channels()){
      cur_channel = 0;
    } else if (cur_channel < 0){
      cur_channel = FM_get_num_channels() - 1;
    }
    
    return FM_select_channel(cur_channel);
  }
  return 0;
}

uint8_t FM_select_frequency(uint16_t freq){
  if (freq){
    radio.setFrequency(freq);
    cur_channel = -1;
    return 1;
  }
  return 0;
}

uint8_t FM_control(uint8_t control, uint8_t on){
  switch (control){
    //case FM_CONTROL_STANDBY:
    //  TEA5767N_standby(on);
    //  break;
    case FM_CONTROL_MUTE:
      radio.setMute(on);
      break;
    case FM_CONTROL_MONO:
      radio.setMono(on);
      break;
    //case FM_CONTROL_HCC:
    //  TEA5767N_highCutControl(on);
    //  break;
    //case FM_CONTROL_SNC:
    //  TEA5767N_stereoNoiseCancelling(on);
    //  break;
    default:
      return 0;
  }
  return 1;
}

fm_channel_info channel_info;
fm_channel_info* FM_channel_info(){
  channel_info.type = 0;
  channel_info.channel = cur_channel;
  channel_info.frequency = radio.getFrequency();

  RADIO_INFO ri;
  radio.getRadioInfo(&ri);
  channel_info.level = ri.rssi;
  channel_info.stereo = ri.stereo;
  
  return &channel_info;
}

fm_state_info state_info;
fm_state_info* FM_state_info(){
  RADIO_INFO ri;
  radio.getRadioInfo(&ri);

  AUDIO_INFO ai;
  radio.getAudioInfo(&ai);
  
  state_info.type = 1;
  state_info.state = 
  /*(TEA5767N_getStereoNoiseCancelling() << 4)
   | (TEA5767N_getHighCutControl() << 3)
   |*/ (ri.mono << 2) 
   | (ai.mute << 1) 
   /*| (TEA5767N_getStandby()<<0)*/;
  
  return &state_info;
}


void RDS_process(uint16_t block1, uint16_t block2, uint16_t block3, uint16_t block4) {
  rds.processData(block1, block2, block3, block4);
}

char* serviceName;
/// Update the ServiceName text on the LCD display.
void DisplayServiceName(char *name){
  serviceName = name;
}

void setup() {
  audio.init();
  audio.volume_percent(0);

  radio.init();
  radio.setVolume(radio.MAXVOLUME);

  //RDS
  //radio.attachReceiveRDS(RDS_process);
  //rds.attachServicenNameCallback(DisplayServiceName);

  EEPROM.begin(512);

  config c;
  for (int i=0; i<sizeof(c); i++){
    *((char*)&c + i) = EEPROM.read(i);
  }
  
  if (!audio.input(c.input)){
    channel(LC75341_INPUT_2);
  }

  audio.gain_dB(c.eq_gain);
  audio.treble_dB(c.eq_treble);
  audio.bass_dB(c.eq_bass);

  if (c.fm_channel >= 0){
    FM_select_channel(c.fm_channel);
  }else if (c.fm_freq > 0){
    FM_select_frequency(c.fm_freq);
  }

  //radio.setMono(false);
  //radio.setMute(false);
  for (int i=0; i<4; i++){
    FM_control(i, bitRead(c.fm_controls, i));
  }

  if (!audio.volume_dB(-c.volume)){
    char data[2];
    data[0] = 0x00;
    data[1] = 30;   //30%
    s_cmd(CLUNET_COMMAND_VOLUME, data, 2);
  }

  if (c.power < 0){
    //power on, send response and write to eeprom, by default
    char data = 1;
    s_cmd(CLUNET_COMMAND_POWER, &data, sizeof(data));
  }else{
    power(c.power);
  }
  
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);
  WiFi.config(ip, gateway, subnet);

  //Wifi connection
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.setHostname("audiobox");

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

  ArduinoOTA.begin();

  irrecv.enableIRIn();

  clunetMulticastBegin();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
    //char r = 404;
    //if (request->args() == 0) {
    //  if (switch_toggle(true)){
    //    r = 200;
    //  }
    //}
    //server_response(request, r);

   request->send_P(200, "text/html", serviceName);
   
  });

  
  server.on("/next", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
    audio.input_next();
    server_response(request, 200); 
  });

  server.on("/prev", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
    audio.input_prev();
    server_response(request, 200); 
  });

   server.on("/vup", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
    audio.volume_up(10);
    server_response(request, 200); 
  });

  server.on("/vdn", HTTP_GET, [](AsyncWebServerRequest *request){  //toggle
    audio.volume_down(10);
    server_response(request, 200); 
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });


  server.on("/f", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("v")){
      String v = request->arg("v");
      radio.setFrequency(v.toInt());
      server_response(request, 200); 
    }

    server_response(request, 404);
  });

  server.onNotFound( [](AsyncWebServerRequest *request) {
    server_response(request, 404);
  });


  server.begin();
}

void server_response(AsyncWebServerRequest *request, unsigned int response) {
  switch (response) {
    case 200:
      request->send(200);
      break;
    case 403:
      request->send(403, "text/plain", "Too frequent request\n\n");
      break;
    default:
      //case 404:
      request->send(404, "text/plain", "File Not Found\n\n");
      break;
  }
}

void savePower(uint8_t on){
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, power), on);
  EEPROM.commit();
}

void saveInput(uint8_t channel){
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, input), channel);
  EEPROM.commit();
}

void saveVolume(uint8_t volume_dB){
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, volume), -volume_dB);
  EEPROM.commit();
}

void saveEqualizer(uint8_t gain_db, int8_t treble_db, uint8_t bass_db){
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, eq_gain), gain_db);
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, eq_treble), treble_db);
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, eq_bass), bass_db);
  EEPROM.commit();
}

void saveFMChannel(fm_channel_info* channel_info){
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, fm_channel), channel_info->channel);
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, fm_freq) + 1, (channel_info->frequency >> 8) & 0xFF);
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, fm_freq) + 0, channel_info->frequency & 0xFF);
  EEPROM.commit();
}

void saveFMControls(fm_state_info* state_info){
  EEPROM.write(EEPROM_CONFIG_ADDRESS + offsetof(config, fm_controls), state_info->state);
  EEPROM.commit();
}

char power_state;
void power(char on){
  power_state = on;
  
  if (on){
    audio.unmute();
  }else{
    audio.mute(); 
  }
  //TODO: FM
  //FM_control(FM_CONTROL_STANDBY, !on);
}

uint8_t check_power(){
  if (!power_state){
    power(1);
    return 5;
  }
  return 0;
}

void check_fm_channel(){
  if (power_state){
    /*FM_control(FM_CONTROL_MUTE, audio.input_value() != LC75341_INPUT_2);*/
  }
}

void channel(uint8_t ch){
  char data[2];
  
  data[0] = 0;
  data[1] = ch;
  b_cmd(CLUNET_COMMAND_CHANNEL, data, 2);
}

void create_cmd(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
  clunet_msg tmp_msg;
  tmp_msg.src_address = src_address;
  tmp_msg.dst_address = dst_address;
  tmp_msg.command = (char)command;
  memcpy(&tmp_msg.data, data, size);
  tmp_msg.size = (char)size;
  
  cmd(&tmp_msg);
}

//команда для себя без выдачи ответа
void s_cmd(unsigned char command, char* data, unsigned char size){
  create_cmd(CLUNET_DEVICE_ID, CLUNET_DEVICE_ID, command, data, size);
}

//команда для выдачи всем
void b_cmd(unsigned char command, char* data, unsigned char size){
  create_cmd(CLUNET_DEVICE_ID, CLUNET_BROADCAST_ADDRESS, command, data, size);
}

void cmd(clunet_msg* m){
  
  //в выключенном состоянии разрешаем только 
  //CLUNET_COMMAND_POWER и CLUNET_COMMAND_CHANNEL
  if (!power_state){
    switch (m->command){
      case CLUNET_COMMAND_POWER:
      case CLUNET_COMMAND_CHANNEL:
      case CLUNET_COMMAND_RC_BUTTON_PRESSED:
        break;
      default:
        return;
    }
  }
  
  char response = 0;
  
  switch(m->command){
    case CLUNET_COMMAND_POWER:
    if (m->size == 1){
      switch (m->data[0]){
        case 0:
        case 1:
          power(m->data[0]);
          response = 5;
          break;
        case 2:
          power(!power_state);
          response = 5;
          break;
        case 0xFF:
          response = 5;
          break;
        }
      }
      break;
    
    case CLUNET_COMMAND_CHANNEL:
    switch (m->size){
      case 1:
      switch (m->data[0]){
        case 0xFF:
          if (power_state){
            response = 1;
          }
          break;
        case 0x01:
          response = check_power();
          if (!response){
            audio.input_next();
            
            check_fm_channel();
            response = 1;
          }
          break;
        case 0x02:
          response = check_power();
          if (!response){
            audio.input_prev();
            
            check_fm_channel();
            response = 1;
          }
          break;
      }
      break;
      case 2:
      switch(m->data[0]){
        case 0x00:
          response = check_power();
          if (!response){
            audio.input(m->data[1] - 1);
            
            check_fm_channel();
            response = 1;
          }
          break;
      }
      break;
    }
    break;
    case CLUNET_COMMAND_VOLUME:
    switch (m->size){
      case 1:
      switch (m->data[0]){
        case 0xFF:
          response = 2;
          break;
        case 0x02:
          audio.volume_up_exp(2);
          response = 2;
          break;
        case 0x03:
          audio.volume_down_exp(2);
          response = 2;
          break;
      }
      break;
      case 2:
      switch(m->data[0]){
        case 0x00:
          audio.volume_percent(m->data[1]);
          response = 2;
          break;
        case 0x01:
          audio.volume_dB(m->data[1]);
          response = 2;
          break;
      }
      break;
    }
    break;
    case CLUNET_COMMAND_MUTE:
    if (m->size == 1){
      switch(m->data[0]){
        case 0:
          audio.volume_percent(0);
          response = 2;
          break;
        case 1:
          audio.mute_toggle();
          response = 2;
          break;
        case 2:
          audio.mute();
          response = 2;
        break;
        case 3:
          audio.unmute();
          response = 2;
        break;
      }
    }
    break;
    case CLUNET_COMMAND_EQUALIZER:
    switch(m->size){
      case 1:
      switch(m->data[0]){
        case 0x00:
          audio.equalizer_reset();
          response = 3;
          break;
        case 0xFF:
          response = 3;
          break;
      }
      break;
      
      case 2:
      case 3:
      switch(m->data[0]){
        case 0x01:  //gain
        switch(m->data[1]){
          case 0x00:  //reset
            audio.gain_dB(0);
            response = 3;
            break;
          case 0x01:  //dB
            if (m->size == 3){
              audio.gain_dB(m->data[2]);
              response = 3;
            }
            break;
          case 0x02:  //+
            audio.gain_up();
            response = 3;
            break;
          case 0x03:  //-
            audio.gain_down();
            response = 3;
            break;
        }
        break;
        
        case 0x02:  //treble
        switch(m->data[1]){
          case 0x00:  //reset
            audio.treble_dB(0);
            response = 3;
          break;
          case 0x01:  //dB
            if (m->size == 3){
              audio.treble_dB(m->data[2]);
              response = 3;
            }
            break;
          case 0x02:  //+
            audio.treble_up();
            response = 3;
            break;
          case 0x03:  //-
            audio.treble_down();
            response = 3;
            break;
        }
        break;
        
        case 0x03:  //bass
        switch(m->data[1]){
          case 0x00:  //reset
            audio.bass_dB(0);
            response = 3;
            break;
          case 0x01:  //dB
            if (m->size == 3){
              audio.bass_dB(m->data[2]);
              response = 3;
            }
          break;
          case 0x02:  //+
            audio.bass_up();
            response = 3;
            break;
          case 0x03:  //-
            audio.bass_down();
            response = 3;
            break;
        }
        break;
      }
      break;
    }
    break;
    case CLUNET_COMMAND_FM:
    if (m->size > 0){
      switch(m->data[0]){
        case 0xFF:  //info
          if (m->size == 2){
            switch (m->data[1]){
             case 0x00:
                response = 10;
                break;
              case 0x01:
                response = 11;
            }
          }
          break;
        case 0x00:  //freq
          if (m->size == 3){
            if (FM_select_frequency((m->data[2]<<8) | (m->data[1]))){ //check band limit
              response = 10;
            }
          }
          break;
        case 0x01:  //saved channel
          if (m->size == 2){
            if (FM_select_channel(m->data[1])){
              response = 10;
            }
          }
          break;
        case 0x02:  //next saved
        case 0x03:  //prev saved
        if (m->size == 1){
          if (FM_select_next_channel(m->data[0] == 0x02)){
            response = 10;
          }
        }
        break;
        
        case 0x05:  //search up
          radio.seekUp(true);
          //if (FM_select_frequency(radio.getFrequency())){
          //  response = 10;
          //}
          break;
          
        case 0x06:  //search down
          radio.seekDown(true);
          //if (FM_select_frequency(radio.getFrequency())){
          //  response = 10;
          //}
          break;
        
        case 0x0A:
          if (m->size == 3){
            if (FM_control(m->data[1], m->data[2])){
              response = 11;
            }
          }
          break;
                
        case 0xEA:  //request num saved channels
          if (m->size == 1){
            m->data[1] = FM_get_num_channels();
            clunetMulticastSend(m->src_address, CLUNET_COMMAND_FM_INFO, m->data, 2);
          }
          break;
        case 0xEB:  //get saved channel's frequency
          if (m->size == 2){
            
            uint16_t* freq = (uint16_t*)(&m->data[2]);
            *freq = FM_get_channel_frequency(m->data[1]);
            clunetMulticastSend(m->src_address, CLUNET_COMMAND_FM_INFO, m->data, 4);
          }
          break;
        case 0xEC:  //add channel
          switch(m->size){
            case 1: { //current freq
              fm_channel_info* info = FM_channel_info();
              m->data[1] = FM_add_channel(info->frequency);
              clunetMulticastSend(m->src_address, CLUNET_COMMAND_FM_INFO, m->data, 2);
              }
              break;
            case 3: { //specified freq
              uint16_t* f = (uint16_t*)&m->data[1];
              m->data[1] = FM_add_channel(*f);
              clunetMulticastSend(m->src_address, CLUNET_COMMAND_FM_INFO, m->data, 2);
              }
              break;
          }
          break;
        case 0xED:  //save channel
          switch(m->size){
            case 2: { //current freq
              fm_channel_info* info = FM_channel_info();
              m->data[1] = FM_save_channel(m->data[1], info->frequency);
              clunetMulticastSend(m->src_address, CLUNET_COMMAND_FM_INFO, m->data, 2);
              }
              break;
            case 4: { //specified freq
              uint16_t* f = (uint16_t*)&m->data[2];
              m->data[1] = FM_save_channel(m->data[1], *f);
              clunetMulticastSend(m->src_address, CLUNET_COMMAND_FM_INFO, m->data, 2);
              }
              break;
          }
          break;
        case 0xEE:
          if (m->size == 3){
            if (m->data[1] == 0xEE && m->data[2] == 0xFF){
              FM_clear_channels();
              m->data[1] = 1;
              clunetMulticastSend(m->src_address, CLUNET_COMMAND_FM_INFO, m->data, 2);
              break;
            }
          }
          break;
      }
    }
    break;
    case CLUNET_COMMAND_RC_BUTTON_PRESSED:{
      if (m->src_address == CLUNET_DEVICE_ID){
        clunetMulticastSend(CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_RC_BUTTON_PRESSED, m->data, m->size);
      }
      
      if (m->data[0] == 0x00){  //nec
        char data[3];
        if (m->data[1] == 0x02){
          switch (m->data[2]){
            case 0x48:{
              data[0] = 2;
              b_cmd(CLUNET_COMMAND_POWER, data, 1);
              }
              break;
            case 0x80:
              channel(1);
              break;
            case 0x40:
              channel(2);
              break;
            case 0xC0:
              channel(3);
              break;
            //case 0x20:
            //  channel(4);
            //  break;
            case 0xF8:{
              data[0] = 2;
              b_cmd(CLUNET_COMMAND_CHANNEL, data, 1);
              }
              break;
            case 0xD8:{
              data[0] = 1;
              b_cmd(CLUNET_COMMAND_CHANNEL, data, 1);
              }
              break;
              
            case 0x08:{
              data[0] = 1;
              b_cmd(CLUNET_COMMAND_MUTE, data, 1);
              }
              break;
            case 0xA0:{
              data[0] = 2;
              data[1] = 2;
              b_cmd(CLUNET_COMMAND_EQUALIZER, data, 2);
              }
              break;
            case 0x10:{
              data[0] = 2;
              data[1] = 3;
              b_cmd(CLUNET_COMMAND_EQUALIZER, data, 2);
              }
              break;
            case 0x60:{
              data[0] = 3;
              data[1] = 2;
              b_cmd(CLUNET_COMMAND_EQUALIZER, data, 2);
              }
              break;
            case 0x90:{
              data[0] = 3;
              data[1] = 3;
              b_cmd(CLUNET_COMMAND_EQUALIZER, data, 2);
              }
              break;
            case 0x00:{
              data[0] = 0;
              b_cmd(CLUNET_COMMAND_EQUALIZER, data, 1);
              }
              break;
            case 0x4A:{
              data[0] = 0x03;
              b_cmd(CLUNET_COMMAND_FM, data, 1);
              }
              break;
            case 0x28:{
              data[0] = 0x02;
              b_cmd( CLUNET_COMMAND_FM, data, 1);
              }
              break;
            
            case 0xda://наше радио (92.9) ->menu
            case 0x9a://мегаполис (103.6) ->ratio
            case 0x88://эхо москвы (99.1) ->brightness
            case 0xc2://вести fm (93.5)   ->exit
              switch (m->data[2]){
                case 0xda:
                  data[1] = 0x4A;
                  data[2] = 0x24;
                  break;
                case 0x9a:
                  data[1] = 0x78;
                  data[2] = 0x28;
                  break;
                case 0x88:
                  data[1] = 0xB6;
                  data[2] = 0x26;
                  break;
                case 0xc2:
                  data[1] = 0x86;
                  data[2] = 0x24;
                  break;
              }
              data[0] = 0x00;
              b_cmd(CLUNET_COMMAND_FM, data, 3);
              break;
          }
        }

        if ((m->data[1] & (~_BV(7))) == 0x02){    //addresss == 0x02, repeat allowed
          switch (m->data[2]){
            case 0x58:{
              delayedResponseCounterValue = millis();
              char data = 2;
              s_cmd(CLUNET_COMMAND_VOLUME, &data, 1);
              }
              break;
            case 0x78:{
              delayedResponseCounterValue = millis();
              char data = 3;
              s_cmd(CLUNET_COMMAND_VOLUME, &data, 1);
              }
              break;
          }
        }
      }
    }
    break;
  }
  
  if (!(m->src_address == CLUNET_DEVICE_ID && m->dst_address == CLUNET_DEVICE_ID)){  //not s_cmd
    char data[3];
    switch(response){
      case 1:{
        data[0] = audio.input_value() + 1;  //0 channel -> to 1 channel
        clunetMulticastSend(m->dst_address, CLUNET_COMMAND_CHANNEL_INFO, data, 1);
        saveInput(data[0]-1);
      }
      break;
      case 2:{
        data[0] = audio.volume_percent_value();
        data[1] = audio.volume_dB_value();
        clunetMulticastSend(m->dst_address, CLUNET_COMMAND_VOLUME_INFO, data, 2);
        saveVolume(data[1]);
      }
      break;
      case 3:{
        data[0] = audio.gain_dB_value();
        data[1] = audio.treble_dB_value();
        data[2] = audio.bass_dB_value();
        clunetMulticastSend(m->dst_address, CLUNET_COMMAND_EQUALIZER_INFO, data, 3);
        saveEqualizer(data[0], data[1], data[2]);
      }
      break;
      case 5:
        clunetMulticastSend(m->dst_address, CLUNET_COMMAND_POWER_INFO, (char*)&power_state, sizeof(power_state));
        savePower(power_state);
        break;

      case 10:{
        fm_channel_info* info = FM_channel_info();
        clunetMulticastSend(m->dst_address, CLUNET_COMMAND_FM_INFO, (char*)info, sizeof(fm_channel_info));
        saveFMChannel(info);
      }
      break;
      case 11:{
        fm_state_info* info = FM_state_info();
        clunetMulticastSend(m->dst_address, CLUNET_COMMAND_FM_INFO, (char*)info, sizeof(fm_state_info));
        saveFMControls(info);
      }
      break;
      
    }
  }
}

void loop() {
  if (irrecv.decode(&ir_results)) {
    if (ir_results.decode_type == NEC){
      if (ir_results.value == kRepeat){
        nec_data[1] |= 0x80;  //7bit 
      }else{
        nec_data[0] = 0x00;
        nec_data[1] = (ir_results.value >> 24) & 0xFF;
        nec_data[2] = (ir_results.value >> 8) & 0xFF;
      }
      s_cmd(CLUNET_COMMAND_RC_BUTTON_PRESSED, nec_data, 3);
    }else{
      nec_data[0] = 0xFF; //reset
    }
    irrecv.resume(); // принимаем следующую команду
  }
  
  clunet_msg msg;
  if (clunetMulticastHandleMessages(&msg)) {
      cmd(&msg);
  }

  if (delayedResponseCounterValue > 0){
    if (millis()-delayedResponseCounterValue > 150){
      char d = 0xFF;
      b_cmd(CLUNET_COMMAND_VOLUME, &d, 1);
      delayedResponseCounterValue = 0;
    }
  }

  radio.checkRDS();

  ArduinoOTA.handle();
  yield();
}
