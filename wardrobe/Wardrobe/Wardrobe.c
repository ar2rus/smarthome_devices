
#include "Wardrobe.h"

volatile unsigned char doorsStateValue;

volatile unsigned int systime = 0;
volatile unsigned int sensor_check_time = 0;

void doorsResponse(unsigned char address){
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_DOOR_INFO, (char*)&doorsStateValue, sizeof(doorsStateValue));
}

ISR(TIMER_COMP_VECTOR){
	++systime;
	
	TIMER_REG = 0;	//reset counter
}

void cmd(clunet_msg* m){
	switch(m->command){
		case CLUNET_COMMAND_DOOR:
		if (m->size == 0){
			doorsResponse(m->src_address);
		}
		break;
	}
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_DOOR:
			clunet_buffered_push(src_address, dst_address, command, data, size);
	}
}


int main(void){
	cli();
	
	DOOR_ADD_INIT;
	DOORS_SENSORS_INIT;
	doorsStateValue = !DOORS_SENSORS_READ;
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_buffered_init();
	clunet_init();
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	while(1){
		
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
		
		if (sensor_check_time != systime){
			unsigned char doors = DOORS_SENSORS_READ;
			
			//check doors
			if (doorsStateValue != doors){
				doorsStateValue = doors;
				doorsResponse(CLUNET_BROADCAST_ADDRESS);
			}
		}
	}
	
	return 0;
}