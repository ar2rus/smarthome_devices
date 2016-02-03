
#include "tx.h"

#include <util/delay.h>
#include <avr/io.h>


void nec_send(char address, char command){
	
	unsigned long data = address;
	data <<= 8;
	data |= (char)(~address);
	data <<= 8;
	data |= command;
	data <<= 8;
	data |= (char)(~command);
		
	//disable interrupts
	TIMSK0 = 0;
	//set port direction
	set_bit(DDRPORT(PWM_PORT), PWM_PIN);
	// When not sending PWM, we want it low
	unset_bit(OUTPORT(PWM_PORT), PWM_PIN);
	
	PWM_SETUP;
	
	TIMER_ENABLE_PWM;
	_delay_us(NEC_HDR_MARK);
	
	TIMER_DISABLE_PWM;
	_delay_us(NEC_HDR_SPACE);
		
	  for (int i = 0; i < 32; i++) {
		  if (data & NEC_TOPBIT) {
			  TIMER_ENABLE_PWM;
			  _delay_us(NEC_BIT_MARK);
			  
			  TIMER_DISABLE_PWM;
			   _delay_us(NEC_ONE_SPACE);
		  }
		  else {
			  TIMER_ENABLE_PWM;
			  _delay_us(NEC_BIT_MARK);

			  TIMER_DISABLE_PWM;
			  _delay_us(NEC_ZERO_SPACE);
		  }
		  data <<= 1;
	  }

	TIMER_ENABLE_PWM;
	 _delay_us(NEC_BIT_MARK);

	TIMER_DISABLE_PWM;
}

// void nec_send_repeat(){
// 	NEC_TX_HI;
// 	_delay_ms(9);
// 	NEC_TX_LO;
// 	_delay_ms(2.25);
// 	
// 	NEC_TX_HI;
// 	_delay_ms(0.560);
// 	NEC_TX_LO;
// }