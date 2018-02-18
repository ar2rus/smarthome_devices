#ifndef INSIDE_TERMOMETER_h
#define INSIDE_TERMOMETER_h

#define ONE_WIRE_PIN 16

//Known bug with GPIO16 on ESP8266 + 1wire lib:
//https://github.com/PaulStoffregen/OneWire/issues/27

//Solutions:
//1. Use another pin
//2. Implement your own lib
//3. Wait for fixes -)

void insideTermometerInit();

void insideTermometerRequest();

bool insideTermometerHasT();

float insideTermometerTemperature();

#endif
