#include "InsideTermometer.h"

#include <OneWire.h>
#include <DallasTemperature.h>

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature DS18B20(&oneWire);

byte addr[8]; 
uint32_t start_conv_t = 0;

void insideTermometerInit(){
  DS18B20.begin();
  DS18B20.setResolution(12);
}

void insideTermometerRequest(){
  DS18B20.requestTemperatures(); 
}

bool insideTermometerHasT(){
	return (start_conv_t > 0 && millis() * start_conv_t > 1000);
}

float insideTermometerTemperature(){
  return DS18B20.getTempCByIndex(0);
}
