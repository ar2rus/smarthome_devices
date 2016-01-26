


#define F_CPU 1200000UL

#include <avr/io.h>
#include <util/delay.h>

#include "tx.h"


//=============================================================================
//   SEND_RF_BYTE
//=============================================================================
void send_rf_byte(char txdat){
	//-------------------------------------------------------
	// This is a pulse period encoded system to send a byte to RF module.
	// Bits are sent MSB first. Each byte sends 9 pulses (makes 8 periods).
	// Timing;
	//   HI pulse width; always 80uS
	//   0 bit, LO width; 20uS (total 0 bit pulse period 100uS)
	//   1 bit, LO width; 70uS (total 1 bit pulse period 150uS)
	//   space between bytes, LO width; 170uS (total space period 250uS)
	//-------------------------------------------------------
	
	// make 250uS start bit first
	_delay_us(170);      // 170uS LO
	RF_HI;
	_delay_us(80-1);     // 80uS HI
	RF_LO;

	// now loop and send 8 bits
	for(char tbit=0; tbit<8; tbit++){
		_delay_us(20-1);             // default 0 bit LO period is 20uS
		if(bit(txdat, 7)){
			_delay_us(50);  // increase the LO period if is a 1 bit!
		}
		RF_HI;
		_delay_us(80-1);             // 80uS HI pulse
		RF_LO;
		txdat <<= 1;                // roll data byte left to get next bit
	}
}


int main(void){
	
	RF_INIT;
	
	while (1){
		send_rf_byte(255);
		send_rf_byte(255);
		for (char i=0; i<8; i++){
			send_rf_byte(i);
		}
		
		_delay_ms(1000);
	}

	return 0; 
}
