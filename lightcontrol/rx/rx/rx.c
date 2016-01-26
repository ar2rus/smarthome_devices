/*
 * rx.c
 *
 * Created: 26.01.2016 20:53:30
 *  Author: gargon
 */ 


#define F_CPU 1200000UL

#include <avr/io.h>
#include <util/delay.h>

#include "rx.h"


unsigned char rxdat[10];  // (global var) holds received RF bytes

//=============================================================================
//   RECEIVE_RF_PACKET
//=============================================================================
void receive_rf_packet(void){
	//-------------------------------------------------------
	// This function receives an RF packet of bytes in my pulse period
	// encoded format. The packet must have 10 valid contiguous bytes
	// or the function will not exit. There is no timeout feature, but could be added.
	// global variable; unsigned char rxdat[10] holds the 10 byte result.
	//-------------------------------------------------------
	unsigned char rrp_data;
	unsigned char rrp_period;
	unsigned char rrp_bits;
	unsigned char rrp_bytes;

	rrp_bytes = 0;
	while(rrp_bytes < 10){   // loop until it has received 10 contiguous RF bytes
		//-----------------------------------------
		// wait for a start pulse >200uS
		while(1){
			while(!RF_VAL) continue;    // wait for input / edge
			while(RF_VAL) continue;     // wait for input \ edge
			rrp_period = TIMER_REG;     // grab the pulse period!
			TIMER_REG = 0;              // and ready to record next period
			if(rrp_period < NUM_TICKS_200_MKS){		  // clear bytecount if still receiving noise
				 rrp_bytes = 0;
			} else {
				break;                   // exit if pulse was >200uS
			}
		}
		
		
		
		//-----------------------------------------
		// now we had a start pulse, record 8 bits
		rrp_bits = 8;
		rrp_data = 0;
		while(rrp_bits){
			while(!RF_VAL) continue;		// wait for input / edge
			while(RF_VAL) continue;			// wait for input \ edge
			rrp_period = TIMER_REG;			// grab the pulse period!
			TIMER_REG = 0;					// and ready to record next period

			if(rrp_period >= NUM_TICKS_200_MKS){			// if >=200uS, is unexpected start pulse!
				 break;
			}

			rrp_data <<= 1;
			if(rrp_period > NUM_TICKS_125_MKS){			//  125uS
				rrp_data |= 1;
			}
			rrp_bits--;                   // and record 1 more good bit done
		}
		
		//-----------------------------------------
		// gets to here after 8 good bits OR after an error (unexpected start pulse)
		if(rrp_bits){        // if error
			rrp_bytes = 0;   // reset bytes, must run from start of a new packet again!
			
			
			
		} else{              // else 8 good bits were received
			rxdat[rrp_bytes] = rrp_data;  // so save the received byte into array
			rrp_bytes++;                  // record another good byte was saved
		}
	}
}
//-----------------------------------------------------------------------------

int main(void){
	
	LED_INIT;
	LED_ON;
	_delay_ms(500);
	LED_OFF;
	
	RF_INIT;
	TIMER_INIT;
	
	
    while(1)
    {
        receive_rf_packet();
		char check = 1;
		
		/*for (unsigned char i=0; i < 10; i++){
			LED_ON;
			_delay_ms(2000);
			LED_OFF;
			_delay_ms(1000);
			for (int j=0; j<rxdat[i]; j++){
			LED_ON;
			_delay_ms(200);
			LED_OFF;
			_delay_ms(200);
			}
			_delay_ms(2000);
		}*/
	
		
		
		for (unsigned char i=1; i < 10; i++){
			if ((rxdat[i] - rxdat[i-1]) != 1){
				
				//LED_ON;
				
				check = 0;
				break;
			}
		}
		
		if (check){
			LED_ON;
			_delay_ms(200);
			LED_OFF;
			_delay_ms(200);
			LED_ON;
			_delay_ms(200);
			LED_OFF;
			_delay_ms(200);
			LED_ON;
			_delay_ms(200);
			LED_OFF;
		}
		
    }
	return 0;
}