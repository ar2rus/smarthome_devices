
#include "Wardrobe.h"

#include <avr/interrupt.h>

volatile unsigned char doorsStateValue;

void doorsResponse(unsigned char address){
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_DOOR_INFO, (char*)&doorsStateValue, sizeof(doorsStateValue));
}

ISR(TIMER1_COMPA_vect){
	sei();	//разрешаем прерывания более высокого приоритета (clunet)
	
	unsigned char doors = DOORS_SENSORS_READ;
	
	//check doors
	if (doorsStateValue != doors){
		doorsStateValue = doors;
		doorsResponse(CLUNET_BROADCAST_ADDRESS);
	}
	
	OCR1A = TCNT1 + TIMER_NUM_TICKS;
}





void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_DOOR:
		if (size == 0){
			doorsResponse(src_address);
		}
		break;
	}
}


int main(void){
	
	cli();
	
	DOOR_ADD_INIT;
	
	DOORS_SENSORS_INIT;
	doorsStateValue = !DOORS_SENSORS_READ;
	
	clunet_init();
	clunet_set_on_data_received(clunet_data_received);
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	sei();
	
	while(1){
		
	}
	
	return 0;
}