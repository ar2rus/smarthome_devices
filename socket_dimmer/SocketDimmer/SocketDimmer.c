
#include "SocketDimmer.h"

signed char switchState(unsigned char id){
	switch(id){
		case RELAY_0_ID:
			return RELAY_0_STATE;
		break;
	}
	return -1;
}

void switchExecute(unsigned char id, unsigned char command){
	switch(command){
		case 0x00:	//откл
		switch(id){
			case RELAY_0_ID:
				RELAY_0_OFF;
			break;
		}
		break;
		case 0x01: //вкл
		switch(id){
			case RELAY_0_ID:
				RELAY_0_ON;
			break;
		}
		break;
		case 0x02: //перекл
		switch(id){
			case RELAY_0_ID:
				RELAY_0_TOGGLE;
			break;
		}
		break;
	}
}

void switchResponse(unsigned char address){
	char info = (RELAY_0_STATE << (RELAY_0_ID-1));
	clunet_send_fairy(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

void cmd(clunet_msg* m){
	switch(m->command){
		case CLUNET_COMMAND_SWITCH:
			if (m->data[0] == 0xFF){	//info request
				if (m->size == 1){
					switchResponse(m->src_address);
				}
			}else{
				if (m->size == 2){
					switch(m->data[0]){
						case 0x00:
						case 0x01:
						case 0x02:
							switchExecute(m->data[1], m->data[0]);
							switchResponse(m->src_address);
							break;
						case 0x03:
							for (char i=0; i<8; i++){
								switchExecute(i+1, bit(m->data[1], i));
							}
							switchResponse(m->src_address);
							break;
					}
				}
			}
			break;
	}
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_SWITCH:
			clunet_buffered_push(src_address, dst_address, command, data, size);
	}
}

uint8_t dimmer_value = 100;

volatile uint8_t low_high = 0;	//rise or fall on zero detector
volatile uint8_t tick;

ISR(ZERO_DETECTOR_INT_VECTOR){
	if (ZERO_DETECTOR_READ == low_high){
		tick = 0;
		if (low_high){
			DISABLE_TIMER_CMP_A;
			RELAY_0_OFF;
		}else{
			ENABLE_TIMER_CMP_A;
		}
		low_high = !low_high;
	}
}


ISR(TIMER_COMP_VECTOR){
	TIMER_COUNTER = 0;	//reset counter
	
	tick = 0;
	if (dimmer_value < tick){
		RELAY_0_ON;
	}
}

int main(void){
	cli();
	
	RELAY_0_INIT;
	RELAY_0_OFF;
	
	ZERO_DETECTOR_INIT;
	ZERO_DETECTOR_INIT_INT;
	
	TIMER_INIT;
	DISABLE_TIMER_CMP_A;
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_init();
	
	while (1){
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
	}
	return 0;
}