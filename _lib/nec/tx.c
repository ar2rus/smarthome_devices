
#include "tx.h"

#include <util/delay.h>
#include <avr/io.h>


void nec_sendByte(char b){
	for (unsigned char i=0; i<8; i++){
		NEC_TX_HI;
		_delay_ms(0.560);
		NEC_TX_LO;
		if (b & 1){
			_delay_ms(1.690);
		}else{
			_delay_ms(0.560);
		}
		b = (b >> 1);
	}
}

void nec_send(char address, char command){
	NEC_TX_HI;
	_delay_ms(9);
	NEC_TX_LO;
	_delay_ms(4.5);
	
	nec_sendByte(address);
	nec_sendByte(~address);
	nec_sendByte(command);
	nec_sendByte(~command);
	
	NEC_TX_HI;
	_delay_ms(0.560);
	NEC_TX_LO;
}

void nec_send_repeat(){
	NEC_TX_HI;
	_delay_ms(9);
	NEC_TX_LO;
	_delay_ms(2.25);
	
	NEC_TX_HI;
	_delay_ms(0.560);
	NEC_TX_LO;
}