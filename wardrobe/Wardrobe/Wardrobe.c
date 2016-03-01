
#include "Wardrobe.h"

unsigned char doors_state_value;

volatile unsigned char systime = 0;

void doorsResponse(unsigned char address){
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_DOOR_INFO, (char*)&doors_state_value, sizeof(doors_state_value));
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


unsigned char sensor_check_time = 0;

int main(void){
	cli();
	
	DOOR_ADD_INIT;
	DOORS_SENSORS_INIT;
	doors_state_value = !DOORS_SENSORS_READ;
	
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
			unsigned char doors_temp_value = DOORS_SENSORS_READ;
			
			//check doors
			if (doors_state_value != doors_temp_value){
				
				unsigned char trigger_light = 0;
				//check should trigger light
				if ((doors_state_value > 0) ^ (doors_temp_value > 0)){
					trigger_light = 1;
				}
				
				doors_state_value = doors_temp_value;
				doorsResponse(CLUNET_BROADCAST_ADDRESS);
				
				if (trigger_light){
					char data[2] = {doors_state_value > 0, WARDROBE_LIGHT_RELAY_ID};
					clunet_send_fairy(WARDROBE_LIGHT_RELAY_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_SWITCH, &data[0], sizeof(data));
				}
			}
			
			sensor_check_time = systime;
		}
	}
	
	return 0;
}