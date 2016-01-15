
#include "Relay_2.h"

volatile unsigned int systime = 0;

void switchExecute(char id, char command){
	switch(command){
		case 0x00:	//откл
		switch(id){
			case RELAY_0_ID:
				RELAY_0_OFF;
			break;
			case RELAY_1_ID:
				RELAY_1_OFF;
			break;
			case RELAY_2_ID:
				RELAY_2_OFF;
			break;
		}
		break;
		case 0x01: //вкл
		switch(id){
			case RELAY_0_ID:
				RELAY_0_ON;
			break;
			case RELAY_1_ID:
				RELAY_1_ON;
			break;
			case RELAY_2_ID:
				RELAY_2_ON;
			break;
		}
		break;
		case 0x02: //перекл
		switch(id){
			case RELAY_0_ID:
				RELAY_0_TOGGLE;
			break;
			case RELAY_1_ID:
				RELAY_1_TOGGLE;
			break;
			case RELAY_2_ID:
				RELAY_2_TOGGLE;
			break;
		}
		break;
	}
}

void switchResponse(unsigned char address){
	char info = (RELAY_2_STATE << (RELAY_2_ID-1)) | (RELAY_1_STATE << (RELAY_1_ID-1)) | (RELAY_0_STATE << (RELAY_0_ID-1));
	clunet_send_fairy(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

void fan_humidity_request(){
	clunet_send_fairy(HUMIDITY_SENSOR_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_HUMIDITY, 0, 0);
}

void fan_control_changed(char on_){
	//не вызывать switchResponse из switchExecute
	//для возможности первоначального переключения нескольких ключей сначала
	//а потом отправки одного респонса на всех
	switchExecute(FAN_RELAY_ID, on_);
	switchResponse(CLUNET_BROADCAST_ADDRESS);
}

void fanResponse(unsigned char address){
	struct fan_info_struct i;
	fan_info(&i);
	clunet_send_fairy(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_FAN_INFO, (char*)&i, sizeof(i));
}

void fan_state_changed(char state){
	fanResponse(CLUNET_BROADCAST_ADDRESS);
}

void cmd(clunet_msg* m){
	switch(m->command){
		
		case CLUNET_COMMAND_SWITCH:
			switch(m->size){
				case 1:
					if (m->data[0] == 0xFF){
						switchResponse(m->src_address);
					}
					break;
				case 2:
					switch(m->data[0]){
						case 0x00:
						case 0x01:
						case 0x02:
							switchExecute(m->data[1], m->data[0]);
							switchResponse(m->src_address);
							break;
						case 0x03:
							for (char i=0; i<8; i++){
								switchExecute((i+1), bit(m->data[1], i));
							}
							switchResponse(m->src_address);
							break;
					}
					break;
			}
			break;
		
		case CLUNET_COMMAND_HUMIDITY_INFO:
			if (m->src_address == HUMIDITY_SENSOR_DEVICE_ID && m->dst_address == CLUNET_DEVICE_ID){
				if (m->size == 2){
					signed int h10 = *(signed int*)m->data;
					if (h10 < 0xFFFF){
						h10 /= 10;
						fan_humidity(h10 & 0xFF);
					}
				}
			}
			break;
		
		case CLUNET_COMMAND_LIGHT_LEVEL_INFO:
			if (m->src_address == LIGHT_SENSOR_DEVICE_ID && m->size==2){
				fan_trigger(m->data[0]);
			}
			break;
			
		case CLUNET_COMMAND_DOOR_INFO:
			if (m->src_address == DOORS_MIRRORED_BOX_DEVICE_ID && m->size==1){
				switchExecute(MIRRORED_BOX_LIGHT_RELAY_ID, m->data[0]>0);
				switchResponse(CLUNET_BROADCAST_ADDRESS);
			}
			break;
			
		case CLUNET_COMMAND_FAN:
			if (m->size == 1){
				switch (m->data[0]){
					case 0x00:
						fan_button();
						break;
					case 0xFF:
						fanResponse(m->src_address);
						break;
				}
			}
			break;
	}
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_SWITCH:
		case CLUNET_COMMAND_HUMIDITY_INFO:
		case CLUNET_COMMAND_LIGHT_LEVEL_INFO:
		case CLUNET_COMMAND_DOOR_INFO:
		case CLUNET_COMMAND_FAN:
			clunet_buffered_push(src_address, dst_address, command, data, size);
	}
}

ISR(TIMER_COMP_VECTOR){
	++systime;
	
	TIMER_REG = 0;	//reset counter
}


volatile unsigned int fan_time = 0;

int main(void){
	cli();
	
	RELAY_0_INIT;
	RELAY_1_INIT;
	RELAY_2_INIT;
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;
	
	fan_init();
	fan_set_on_humidity_request(fan_humidity_request);
	fan_set_on_control_changed(fan_control_changed);
	fan_set_on_state_changed(fan_state_changed);
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_buffered_init();
	clunet_init();
	
	
	while (1){
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
		
		if (fan_time != systime){
			fan_time = systime;
			fan_tick_second();
		}
	}
	
	return 0;
}