
#include "Relay_2.h"

#include <avr/interrupt.h>
#include <util/delay.h>


void switchResponse(unsigned char address){
	sei();
	while (clunet_ready_to_send());	//ожидание отправки других сообщений
	
	char info = (RELAY_2_STATE << (RELAY_2_ID-1)) | (RELAY_1_STATE << (RELAY_1_ID-1)) | (RELAY_0_STATE << (RELAY_0_ID-1));
	clunet_send(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

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
	switchResponse(CLUNET_BROADCAST_ADDRESS);
}

void fanResponse(unsigned char address){
	sei();
	while (clunet_ready_to_send());//ожидание отправки других сообщений
	
	struct fan_info_struct i;
	fan_info(&i);
	
	clunet_send(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_FAN_INFO, (char*)&i, sizeof(i));
}

void fan_humidity_request(){
	clunet_send(HUMIDITY_SENSOR_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_HUMIDITY, 0, 0);
}

void fan_control_changed(char on_){
	switchExecute(FAN_RELAY_ID, on_);
}

void fan_state_changed(char state){
	fanResponse(CLUNET_BROADCAST_ADDRESS);
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_SWITCH:
		if (data[0] == 0xFF){	//info request
			if (size == 1){
				switchResponse(src_address);
			}
		}else{
			if (size == 2){
				switch(data[0]){
					case 0x00:
					case 0x01:
					case 0x02:
						switchExecute(data[1], data[0]);
					break;
					case 0x03:
						for (char i=0; i<8; i++){
							switchExecute((i+1), bit(data[1], i));
						}
					break;
				}
			}
		}
		break;
		case CLUNET_COMMAND_HUMIDITY_INFO:
			if (src_address == HUMIDITY_SENSOR_DEVICE_ID && dst_address == CLUNET_DEVICE_ID){
				if (size == 2){
					signed int h10 = *(signed int*)data;
					if (h10 < 0xFFFF){
						h10 /= 10;
						fan_humidity(h10 & 0xFF);
					}
				}
			}
		break;
		case CLUNET_COMMAND_LIGHT_LEVEL_INFO:
			if (src_address == LIGHT_SENSOR_DEVICE_ID && size==2){
				fan_trigger(data[0]);
			}
		break;
		case CLUNET_COMMAND_FAN:
			if (size == 1){
				switch (data[0]){
					case 0x00:
						fan_button();
					break;
					case 0xFF:
						fanResponse(src_address);
					break;
				}
			}	
		break;
	}
}


int main(void){
	cli();
	
	RELAY_0_INIT;
	RELAY_1_INIT;
	RELAY_2_INIT;
	
	fan_init();
	fan_set_on_humidity_request(fan_humidity_request);
	fan_set_on_control_changed(fan_control_changed);
	fan_set_on_state_changed(fan_state_changed);
	
	clunet_init();
	clunet_set_on_data_received(clunet_data_received);
	
	
	sei();
	
	while (1){
	}
	return 0;
}