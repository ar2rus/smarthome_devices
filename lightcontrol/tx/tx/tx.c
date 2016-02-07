
#include <util/delay.h>
#include "tx.h"


int main(void){
	
	RF_TX_INIT;
	
	 	char data[RF_RGB_LIGHTS_DATA_LEN];
	 	for (int i=0; i<RF_RGB_LIGHTS_DATA_LEN; i++){
	 		data[i] = i+1;
	 	}
	BTN_INIT;
	char btn_val = BTN_VAL;
	while (1){
		//for (char i=0; i<100; i++){
		//	rf_send_byte(i);
		//}
// 		
// 		rf_send_byte(0XE2);
// 		rf_send_byte(0XE2);
// 		rf_send_byte(0XE2);
// 		rf_send_byte(0XE2);
// 		rf_send_byte(0XE2);

if (BTN_VAL != btn_val){
	btn_val = BTN_VAL;
	
	if (btn_val){
		data[0] = 0x68;
	}else{
		data[0] = 0xb0;
	}
	
	rf_send_message(RF_RGB_LIGHTS_ID, data, 5);
	
	_delay_ms(100);
	//_delay_ms(2000);
}


// 		data[0] = 0x68;
// 		rf_send_message(RF_RGB_LIGHTS_ID, data, 3);
// 		//_delay_ms(10);
//  		data[0] = 0x98;
// 		rf_send_message(RF_RGB_LIGHTS_ID, data, 3);
// 		//_delay_ms(100);
// 		data[0] = 0xb0;
// 		rf_send_message(RF_RGB_LIGHTS_ID, data, 3);
// 		//_delay_ms(100);
// 
//  	}
	
	
// 	char data[RF_RGB_LIGHTS_DATA_LEN];
// 	for (int i=0; i<RF_RGB_LIGHTS_DATA_LEN; i++){
// 		data[i] = i+1;
// 	}
// 	
// 	while (1){		
// 		rf_send_message(RF_RGB_LIGHTS_ID, data, RF_RGB_LIGHTS_DATA_LEN, 1);
// 		
// 		_delay_ms(1000);
 	}

	return 0; 
}