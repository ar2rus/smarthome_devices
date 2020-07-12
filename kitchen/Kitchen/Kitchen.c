
#include "Kitchen.h"


volatile char buttonStateValue;
volatile char hallSensorValue;

volatile uint32_t systime = 0;

volatile uint32_t c_time = 0;
volatile uint32_t button_pressed_time = 0;

//набираемый на пульте номер (максимально NUMBER_DIAL_MAX_LENGTH символов)
char number_dial[NUMBER_DIAL_MAX_LENGTH];
//количество набранных символов
uint8_t number_dial_cnt = 0;
//время набора последнего символа (для анализа таймаута)
uint32_t number_dial_time = 0;

void switchResponse(unsigned char address){
	char info = (RELAY_1_STATE << (RELAY_1_ID-1)) | (RELAY_0_STATE << (RELAY_0_ID-1));
	clunet_send_fairy(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

void buttonStateResponse(unsigned char address, unsigned char button_id, unsigned char state){
	char data[] = {button_id, state};
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_BUTTON_INFO, data, sizeof(data));
}

void buttonResponse(unsigned char address, unsigned char button_id){
	buttonStateResponse(address, button_id, buttonStateValue);
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

void numberDialResponse(uint8_t type){
	char data[NUMBER_DIAL_MAX_LENGTH + 1];
	data[0] = type;
	char size = 1;
	
	switch (type){
		case 0x01:
		case 0x02:
			memcpy(&data[1], &number_dial, number_dial_cnt);
			size += number_dial_cnt;
			break;
		case 0x03:	
			break;
		default:	//неизвестный тип
			return;
	}
	clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, COMMAND_ROTARY_DIAL_NUMBER_INFO, data, size);
}

void nextDigitDialed(unsigned char digit){
	if (number_dial_cnt < NUMBER_DIAL_MAX_LENGTH){
		number_dial_time = systime;
		number_dial[number_dial_cnt++] = digit;
		numberDialResponse(0x02);	//набираемый номер
	}else{
		numberDialResponse(0x03);	//превышена длина номера
		number_dial_cnt = 0;
	}
}

void numberDialed(){
	if (number_dial_cnt){
		number_dial_time = 0;
		
		numberDialResponse(0x01);
		number_dial_cnt = 0;
	}
}

void checkNumberTimeout(){
	if (number_dial_time){
		if (systime - number_dial_time > NUMBER_DIAL_TIMEOUT){
			numberDialed();
		}
	}
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
			clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_DEVICE_ID, CLUNET_COMMAND_RC_BUTTON_PRESSED, data, sizeof(data));
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
				buttonStateResponse(m->src_address, BUTTON_ID, buttonStateValue ? (button_pressed_time ? 1 : 0) : 0);
				buttonStateResponse(m->src_address, BUTTON_LONG_ID, buttonStateValue ? (!button_pressed_time ? 1 : 0) : 0);
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
			if (m->src_address == CLUNET_DEVICE_ID){
				clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_RC_BUTTON_PRESSED, m->data, m->size);
				
				if (m->size == 3){
					if (m->data[0] == 0x00){
						if (m->data[1] == 0x00){	//address == 0, no repeats
							char data[2];
							switch(m->data[2]){
								case 0x68:	//mute
									data[0] = 0x02;
									clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_MUTE, data, 1);
								break;
								case 0xC8:	//power
									data[0] = 0x00;
									clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_POWER, data, 1);
								break;
								case 0x28:	//fan toggle
									switchExecute(FAN_RELAY_ID, 0x02);
									switchResponse(CLUNET_BROADCAST_ADDRESS);
								break;
								
								case 0xCA:	//kitchen light off
									data[0] = 0xFF;
									data[1] = 0;
									clunet_send_fairy(CLUNET_KITCHEN_LIGHT_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_DIMMER, data, 2);
								break;
								case 0x4A:	//kitchen light dimmer = 33%
									data[0] = 0xFF;
									data[1] = 0xFF / 3;
									clunet_send_fairy(CLUNET_KITCHEN_LIGHT_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_DIMMER, data, 2);
								break;
								case 0x8A:	//kitchen light dimmer = 66%
									data[0] = 0xFF;
									data[1] = 0xFF * 2 / 3;
									clunet_send_fairy(CLUNET_KITCHEN_LIGHT_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_DIMMER, data, 2);
								break;
								case 0x0A:	//kitchen light on
									data[0] = 0xFF;
									data[1] = 0xFF;
									clunet_send_fairy(CLUNET_KITCHEN_LIGHT_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_DIMMER, data, 2);
								break;
								
								case 0x80:
									nextDigitDialed(1);	
								break;
								case 0x40:
									nextDigitDialed(2);
								break;
								case 0xC0:
									nextDigitDialed(3);
								break;
								case 0x20:
									nextDigitDialed(4);
								break;
								case 0xA0:
									nextDigitDialed(5);
								break;
								case 0x60:
									nextDigitDialed(6);
								break;
								case 0xE0:
									nextDigitDialed(7);
								break;
								case 0x10:
									nextDigitDialed(8);
								break;
								case 0x90:
									nextDigitDialed(9);
								break;
								case 0x00:
									nextDigitDialed(0);
								break;
								case 0x08:	//enter
									numberDialed();	//номер набран и подтвержден
								break;	
							}
						}
					}
				}
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
		
		if (systime - c_time >= 10){	//	check the button and the hall sensor every 10 ms only
				char state = BUTTON_READ;
				if (state != buttonStateValue){
					if (state){
						button_pressed_time = systime;
					}else{
						if (button_pressed_time){
							buttonResponse(CLUNET_BROADCAST_ADDRESS, BUTTON_ID);
							switchExecute(FAN_RELAY_ID, 0x02);	//toggle
							switchResponse(CLUNET_BROADCAST_ADDRESS);
							buttonStateResponse(CLUNET_BROADCAST_ADDRESS, BUTTON_ID, 0);
						}else{
							buttonStateResponse(CLUNET_BROADCAST_ADDRESS, BUTTON_LONG_ID, 0);
						}
					}
					buttonStateValue = state;
				}else{
					if (buttonStateValue && button_pressed_time){
						if (systime - button_pressed_time >= 1500){
							button_pressed_time = 0;
							buttonResponse(CLUNET_BROADCAST_ADDRESS, BUTTON_LONG_ID);
						}
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
		
		checkNumberTimeout();
	}
	
	return 0;
}