
#include "Kitchen.h"


char buttonStateValue;
char hallSensorValue;

volatile uint32_t systime = 0;


void switchResponse(unsigned char address){
	char info = (RELAY_1_STATE << (RELAY_1_ID-1)) | (RELAY_0_STATE << (RELAY_0_ID-1));
	clunet_send_fairy(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

void buttonResponse(unsigned char address){
	char data[] = {BUTTON_ID, buttonStateValue};
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_BUTTON_INFO, data, sizeof(data));
}

void exhaustedFanResponse(unsigned char address){
	char data[] = {EXHAUST_FAN_DEVICE_ID, hallSensorValue};
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_DEVICE_STATE_INFO, data, sizeof(data));
}

void temperatureResponse(unsigned char address){
	
	int16_t temperature;
	
	char data[5];
	
	if (dht_gettemperature_cached(&temperature, systime)){
		data[0] = 1;
		data[1] = 1;
		data[2] = DHT_SENSOR_ID;
		
		memcpy(&data[3], &temperature, 2);
	}else{
		data[0] = 0;
	}
	
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_TEMPERATURE_INFO, data, 1 + 4 * data[0]);
}

void humidityResponse(unsigned char address){
	int16_t humidity;
	
	char data[2];
	
	if (dht_gethumidity_cached(&humidity, systime)){
		memcpy(&data[0], &humidity, 2);
	}else{
		data[0] = 0xFF;
		data[1] = 0xFF;
	}
	
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_HUMIDITY_INFO, data, sizeof(data));
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
		}
		break;
	}
}

ISR(TIMER1_COMPA_vect){
	sei();
	
	++systime;
	
	if (necCheckSignal()){
		OCR1B  = TIMER_REG + NEC_TIMER_CMP_TICKS;
		ENABLE_TIMER_CMP_B;
	}
	
		uint8_t nec_address;
		uint8_t nec_command;
		uint8_t nec_repeated;
		
		if (necValue(&nec_address, &nec_command, &nec_repeated)){
			char data[3];
			data[0] = 0x00;
			data[1] = nec_address;
			if (nec_repeated){
				data[1] |= _BV(7);
			}
			data[2] = nec_command;
			clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_RC_BUTTON_PRESSED, data, sizeof(data));
		}
		
		
		OCR1A = TIMER_REG + TIMER_NUM_TICKS;
}

ISR(TIMER_COMP_B_VECTOR){
	if (necReadSignal()){
		OCR1B += NEC_TIMER_CMP_TICKS;
		}else{
		DISABLE_TIMER_CMP_B;
	}
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
								switchExecute((i+1), bit(m->data[1], i));
							}
							switchResponse(m->src_address);
							break;
					}
				}
			}
			break;
		case CLUNET_COMMAND_TEMPERATURE:
			switch (m->size){
				case 1:
					if (m->data[0] == 0){
						temperatureResponse(m->src_address);
					}
					break;
				case 2:
					if (m->data[0] == 1 && m->data[1] == 1){
						temperatureResponse(m->src_address);
					}
					break;
				case 3:
					if (m->data[0] == 2 && m->data[1] == 1 && m->data[2] == DHT_SENSOR_ID){
						temperatureResponse(m->src_address);
					}
					break;
			}
			break;
		case CLUNET_COMMAND_HUMIDITY:
			if (m->size == 0){
				humidityResponse(m->src_address);
			}
			break;
		case CLUNET_COMMAND_BUTTON:
			if (m->size == 0){
				buttonResponse(m->src_address);
			}
			break;
		case CLUNET_COMMAND_DEVICE_STATE:
			if (m->size == 2){
				switch (m->data[0]){
					case EXHAUST_FAN_DEVICE_ID:
						if (m->data[1] == 0xFF){
							exhaustedFanResponse(m->src_address);
						}
					break;
				}
			}
			break;
		case CLUNET_COMMAND_RC_BUTTON_PRESSED:
			if (m->src_address == CLUNET_BROADCAST_ADDRESS){
				clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_RC_BUTTON_PRESSED, m->data, m->size);
			}
			break;
	}
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_SWITCH:
		case CLUNET_COMMAND_TEMPERATURE:
		case CLUNET_COMMAND_HUMIDITY:
		case CLUNET_COMMAND_BUTTON:
		case CLUNET_COMMAND_DEVICE_STATE:
		case CLUNET_COMMAND_RC_BUTTON_PRESSED:
			clunet_buffered_push(src_address, dst_address, command, data, size);
	}
}


volatile unsigned int c_time = 0;

int main(void){
	
	cli();
	
	RELAY_0_INIT;
	RELAY_1_INIT;
	
	BUTTON_INIT;
	buttonStateValue = BUTTON_READ;
	
	HALL_SENSOR_INIT;
	hallSensorValue = HALL_SENSOR_READ;
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_init();
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	
	while(1){
		
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
		
		if (c_time != systime){	//	check the button and the hall sensor every 10 ms only
			if (c_time-systime >= 10){
					char state = BUTTON_READ;
					if (state != buttonStateValue){
						buttonStateValue = state;
						
						if (buttonStateValue){
							buttonResponse(CLUNET_BROADCAST_ADDRESS);
							switchExecute(FAN_RELAY_ID, 0x02);	//toggle
							switchResponse(CLUNET_BROADCAST_ADDRESS);
						}
					}
					
					state = HALL_SENSOR_READ;
					if (state != hallSensorValue){
						hallSensorValue = state;
						exhaustedFanResponse(CLUNET_BROADCAST_ADDRESS);
						
						switchExecute(FAN_RELAY_ID, hallSensorValue);
						switchResponse(CLUNET_BROADCAST_ADDRESS);
					}
					c_time = systime;
			}
		}
	}
	
	return 0;
}