#ifndef Audiobox_h
#define Audiobox_h


#define EEPROM_CONFIG_ADDRESS 0x00

typedef struct{
  int8_t power;
  uint8_t input;
  int8_t volume;
  
  uint8_t eq_gain;
  int8_t eq_treble;
  uint8_t eq_bass;
  
  int8_t fm_channel;
  uint16_t fm_freq;
  uint8_t fm_controls;
} config;

typedef struct{
  uint8_t type;       //Тип ответа (всегда 0)
  int8_t channel;     //Номер текущего канала
  uint16_t frequency; //Частота
  uint8_t level;      //Уровень сигнала
  uint8_t stereo;     //Стерео
} fm_channel_info;

fm_channel_info* FM_channel_info();

typedef struct{
  uint8_t type;     //Тип ответа (всегда 1)
  uint8_t state;    //0 bit - standby, 1 bit - mute, 2 bit - mono, 3 bit - hcc, 4 bit - snc
} fm_state_info;

fm_state_info* FM_state_info();

#define FM_MAX_NUM_CHANNELS  30
#define FM_PROGRAMS_EEPROM_OFFSET 0x40


//#define FM_CONTROL_STANDBY 0
#define FM_CONTROL_MUTE 1
#define FM_CONTROL_MONO 2
//#define FM_CONTROL_HCC 3
//#define FM_CONTROL_SNC 4

#endif
