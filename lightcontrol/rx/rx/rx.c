
#include "rx.h"
#include <util/delay.h>


char data[10];

int main(void){
	
	LED_INIT;
	LED_ON;
	_delay_ms(500);
	LED_OFF;
	
	RF_RX_INIT;
	
	NEC_TX_INIT;

    while(1){
		
		nec_send(0, 0x30);
		_delay_ms(500);
		nec_send(0, 0x5A);
		_delay_ms(500);
		
//        rf_receive_packet(&data[0], 1, 0);

//		if (data[0]==4){
//			LED_ON;
//		}
		
// 		for (int i=0; i<8; i++){
// 			if (test_bit(data[0], i)){
// 				LED_ON;
// 				_delay_ms(500);
// 				LED_OFF;
// 				_delay_ms(500);
// 			}else{
// 				LED_ON;
// 				_delay_ms(200);
// 				LED_OFF;
// 				_delay_ms(200);
// 			}
			
//		}
		
//		_delay_ms(2000);
//		LED_OFF;
//		_delay_ms(100);
		
// 		char check = 1;
// 		for (unsigned char i=1; i < 10; i++){
// 			if (data[i]-data[i-1] != 1){
// 				check = 0;
// 			}
// 		}
// 		
// 		if (check){
// 			LED_ON;
// 			_delay_ms(200);
// 			LED_OFF;
// 			_delay_ms(200);
// 			LED_ON;
// 			_delay_ms(200);
// 			LED_OFF;
// 			_delay_ms(200);
// 			LED_ON;
// 			_delay_ms(200);
// 			LED_OFF;
// 		}else{
// 			LED_ON;
// 			_delay_ms(1000);
// 			LED_OFF;
// 			_delay_ms(1000);
// 		}
	}

// if (TCNT0 == RF_NUM_TICKS_BIT){
// 	
// 	LED_ON;
// 	_delay_us(10);
// 	LED_OFF;
// 	TCNT0 = 0;
// }

	
	
	
// 	char data[10];
// 	while (1){
// 		if (rf_recieve_message(RF_RGB_LIGHTS_ID, &data[0])){
// 			char check = 1;
// 			for (unsigned char i=1; i < 10; i++){
// 				if (data[i]-data[i-1] != 1){
// 					check = 0;
// 				}
// 			}
// 			
// 			if (check){
// 				LED_ON;
// 				_delay_ms(200);
// 				LED_OFF;
// 				_delay_ms(200);
// 				LED_ON;
// 				_delay_ms(200);
// 				LED_OFF;
// 				_delay_ms(200);
// 				LED_ON;
// 				_delay_ms(200);
// 				LED_OFF;
// 			}
// 		}
// 	}
	
	return 0;
}