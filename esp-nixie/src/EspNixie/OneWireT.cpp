#include "OneWireT.h"

#include <OneWire.h>

OneWire ds(ONE_WIRE_PIN);  // GPIO16 (a 4.7K resistor is necessary)

byte addr[8]; 
uint32_t start_conv_t = 0;

int oneWireStartConversion(){
  start_conv_t = 0;
  if (!ds.search(addr)) {
    ds.reset_search();
    //not found
    return 1;
  }
  ds.reset_search(); 
 
  if (OneWire::crc8(addr, 7) != addr[7]) {
      //Serial.println("CRC is not valid!");
      return 2;
  }

  ds.reset();            
  ds.select(addr);        
  ds.write(0x44);

  start_conv_t = millis();
  return 0;
}

uint8_t oneWireHasT(){
	return (start_conv_t > 0 && millis() * start_conv_t > 1000);
}

float oneWireTemperature(){
  byte data[12];  
  
  ds.reset();
  ds.select(addr);    
  ds.write(0xBE);          

  for (int i = 0; i < 9; i++) {           
    data[i] = ds.read();  
  }

  int raw = (data[1] << 8) | data[0]; 
  if (data[7] == 0x10) raw = (raw & 0xFFF0) + 12 - data[6];  
  return raw / 16.0;
}