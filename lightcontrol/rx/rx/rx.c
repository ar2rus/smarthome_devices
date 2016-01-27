
#include "rx.h"
#include <util/delay.h>

char data[10];

int main(void){
	
	LED_INIT;
	LED_ON;
	_delay_ms(500);
	LED_OFF;
	
	RF_RX_INIT;
	
    while(1){
        rf_receive_packet(&data[0], sizeof(data), 0);
		
		char check = 1;
		for (unsigned char i=1; i < 10; i++){
			if (data[i]-data[i-1] != 1){
				check = 0;
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