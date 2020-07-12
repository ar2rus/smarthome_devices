#include <inttypes.h>
#include "SanyoCCB.h"

SanyoCCB::SanyoCCB(uint8_t do_pin, uint8_t cl_pin, uint8_t ce_pin) {
	_do_pin = do_pin;
	_cl_pin = cl_pin;
	_ce_pin = ce_pin;
}

void SanyoCCB::init() {
	pinMode(_do_pin, OUTPUT);
	pinMode(_cl_pin, OUTPUT);
	pinMode(_ce_pin, OUTPUT);

	digitalWrite(_do_pin, LOW);
	digitalWrite(_cl_pin, LOW);  // Clock-rest-low mode

	// Paranoia: cycle CE to "flush" de bus
	digitalWrite(_ce_pin, HIGH); delayMicroseconds(CCB_DELAY);
	digitalWrite(_ce_pin, LOW); delayMicroseconds(CCB_DELAY);	
}

void SanyoCCB::writeByte(byte data) {
	// Send one byte out via CCB bus (LSB first)
	for(int8_t i = 0; i <8; i++) {
		digitalWrite(_do_pin, bitRead(data, i));
		digitalWrite(_cl_pin, HIGH); delayMicroseconds(CCB_DELAY);
		digitalWrite(_cl_pin, LOW); delayMicroseconds(CCB_DELAY);
	}
}

void SanyoCCB::write(byte address, byte *data, int8_t dataLength) {
	writeByte(address);

  // Enter the data transfer mode
  digitalWrite(_cl_pin, LOW);
  digitalWrite(_ce_pin, HIGH); delayMicroseconds(CCB_DELAY);

  // Send data
  // Note: as CCB devices usually reads registers data from MSB to LSB, the buffer is read from left to right
  for(int i = 0; i < dataLength; i++){
       writeByte(data[i]);
  }
  digitalWrite(_do_pin, LOW);
  digitalWrite(_ce_pin, LOW); delayMicroseconds(CCB_DELAY);
}
