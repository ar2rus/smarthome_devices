#include "Nixie.h"

#include <SPI.h>
#include "Arduino.h"


extern "C" {
  #include "user_interface.h"
}

char nixie_cnt;
char nixie_digits[DIGITS_COUNT];

os_timer_t esp_timer;

void nixieTimerCallback(void *pArg) {
    if (++nixie_cnt > 2){
        nixie_cnt = 0;
    }

    SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
    digitalWrite(SPI_PIN_SS, LOW);
    char d_h = nixie_digits[3+nixie_cnt];
    SPI.transfer(digit_group_h(d_h, nixie_cnt==0, nixie_cnt==1, nixie_cnt==2));
    char d_l = nixie_digits[0+nixie_cnt];
    SPI.transfer(digit_group_l(d_l, nixie_cnt==0, nixie_cnt==1, nixie_cnt==2));
    digitalWrite(SPI_PIN_SS, HIGH); 
    SPI.endTransaction();
}

void nixie_init(){
  nixie_clear();
  
  pinMode(SPI_PIN_SS, OUTPUT);
  SPI.begin();
  os_timer_setfn(&esp_timer, nixieTimerCallback, NULL);
  os_timer_arm(&esp_timer, NIXIE_UPDATE_PERIOD, true);
}

void nixie_set(char d0, char d1, char d2, char d3, char d4, char d5){
    nixie_digits[0]=d0;
    nixie_digits[1]=d1;
    nixie_digits[2]=d2;
    nixie_digits[3]=d3;
    nixie_digits[4]=d4;
    nixie_digits[5]=d5;
}

void nixie_clear(){
  char d = digit_off;
  nixie_set(d,d,d,d,d,d);
}

uint8_t nixie_int(uint32_t v, uint8_t pos_1, char* digits){
  if (pos_1 >= 0 && pos_1 < DIGITS_COUNT){
    int8_t pos = pos_1;
    do{
      uint8_t d = v % 10;
      v /= 10;
      digits[pos] = d;
    }while(--pos >= 0 && v > 0);
    pos++;
    
    for (int i=0; i<DIGITS_COUNT; i++){
      digits[i] = digit_code(digits[i], i>=pos && i<=pos_1, 0);
    }

    return 1;
  }
  return 0;
}

void nixie_set(uint32_t v, uint8_t pos_1){
  char digits[DIGITS_COUNT];

  if (nixie_int(v, pos_1, digits)){
    nixie_set(digits[0],digits[1],digits[2],digits[3],digits[4],digits[5]);
  }
}

void nixie_set(float v, uint8_t pos_1, int num_frac){
  for (int i=0; i<num_frac; i++){
    v *= 10;
  }
  uint32_t b = (int)abs(v);

  char digits[DIGITS_COUNT];
  if (nixie_int(v, pos_1+num_frac, digits)){
    digits[pos_1] = digit_code(digit_value(digits[pos_1]), 1, 1); //add point
    nixie_set(digits[0],digits[1],digits[2],digits[3],digits[4],digits[5]);
  }
}
