
#include <util/delay.h>
#include "tx.h"

int main(void){
	
	RF_TX_INIT;
	
	while (1){
		//for (char i=0; i<100; i++){
		//	rf_send_byte(i);
		//}
		
		rf_send_byte(4);
		
		_delay_ms(500);
	}
	
	
// 	char data[RF_RGB_LIGHTS_DATA_LEN];
// 	for (int i=0; i<RF_RGB_LIGHTS_DATA_LEN; i++){
// 		data[i] = i+1;
// 	}
// 	
// 	while (1){		
// 		rf_send_message(RF_RGB_LIGHTS_ID, data, RF_RGB_LIGHTS_DATA_LEN, 1);
// 		
// 		_delay_ms(1000);
// 	}

	return 0; 
}