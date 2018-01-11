#include "Nixie.h"

#include "Arduino.h"

#include <SPI.h>
#include <Ticker.h>

//#include "SerialDebug.h"

volatile char nixie_cnt;
volatile char nixie_digits[DIGITS_COUNT];

Ticker esp_ticker;

void nixie_update() {
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
  #if DEBUG
  Serial.println("Nixie initialization");
  #endif
  
  pinMode(SPI_PIN_SS, OUTPUT);
  SPI.begin();
  
  nixie_clear();
  
  esp_ticker.attach_ms(NIXIE_UPDATE_PERIOD, nixie_update);
}

/*String digitToString(char d){
  if (digit_enable(d)){
    return String(digit_value(d)) + (digit_point(d) ? "." : "");
  }else{
    return " ";
  }
}*/

void nixie_set(char d0, char d1, char d2, char d3, char d4, char d5){
    nixie_digits[0]=d0;
    nixie_digits[1]=d1;
    nixie_digits[2]=d2;
    nixie_digits[3]=d3;
    nixie_digits[4]=d4;
    nixie_digits[5]=d5;

    //for (int i=0; i<DIGITS_COUNT; i++){
    //  Serial.print(digitToString(nixie_digits[i]));
    // Serial.print(";");
    //}
    //Serial.println();
}

void nixie_clear(){
  char d = digit_off;
  nixie_set(d, d, d, d, d, d);
}

uint8_t nixie_int(uint32_t v, uint8_t pos_1, uint8_t min_num_digits, char* digits){
  if (min_num_digits >=1 && pos_1 >= 0 && pos_1 < DIGITS_COUNT){

    int8_t pos = pos_1;
    do{
      digits[pos] = v % 10;
      v /= 10;
    }while(--pos >= 0 && (v > 0 || (pos_1-pos<min_num_digits)));
    pos++;
    
    for (int i=0; i<DIGITS_COUNT; i++){
      digits[i] = digit_code(digits[i], (i>=pos && i<=pos_1), 0);
    }
    return 1;
  }
  return 0;
}

void nixie_set(uint32_t v, uint8_t pos_1){
  char digits[DIGITS_COUNT];

  if (nixie_int(v, pos_1, 1, digits)){
    nixie_set(digits[0], digits[1], digits[2], digits[3], digits[4], digits[5]);
  }
}

void nixie_set(float v, uint8_t pos_1, int num_frac){
  for (int i=0; i<num_frac; i++){
    v *= 10;
  }
  uint32_t b = (uint32_t)abs(v);

  char digits[DIGITS_COUNT];
  if (nixie_int(b, pos_1+num_frac, num_frac+1, digits)){
    digits[pos_1] = digit_code(digit_value(digits[pos_1]), 1, 1); //add point
    nixie_set(digits[0], digits[1], digits[2], digits[3], digits[4], digits[5]);
  }
}
