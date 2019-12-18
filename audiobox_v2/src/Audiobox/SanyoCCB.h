#ifndef SanyoCCB_h
#define SanyoCCB_h

#include <inttypes.h>
#include "Arduino.h"

class SanyoCCB {
	public:
		SanyoCCB(uint8_t, uint8_t, uint8_t);
		void init();
		void read(byte, byte*, int8_t);
		void write(byte, byte*, int8_t);
	private:
		void writeByte(byte);
		int _do_pin;
		int _cl_pin;
		int _di_pin;
		int _ce_pin;
};

#endif
