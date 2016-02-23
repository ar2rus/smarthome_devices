
#include "Kitchen.h"


char buttonStateValue;
char hallSensorValue;

volatile uint32_t systime = 0;


//char motionSensorValue;
//char lightnessSensorValue;


/*

void motionResponse(unsigned char address){
	char data = motionSensorValue;
	clunet_send(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_MOTION_INFO, &data, sizeof(data));
}

void lightnessResponse(unsigned char address){
	char data[2];
	data[0] = lightnessSensorValue > LIGHTNESS_BARRIER;
	data[1] = lightnessSensorValue;
	
	clunet_send(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_LIGHT_LEVEL_INFO, data, sizeof(data));
}

*/

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
	switchResponse(CLUNET_BROADCAST_ADDRESS);
}

//lightness
/*ISR(ADC_vect){
	char value_ = lightnessSensorValue;
	lightnessSensorValue = 100 * ADCH / 255;
	if (value_ >= 0 && ((lightnessSensorValue > LIGHTNESS_BARRIER) ^ (value_ > LIGHTNESS_BARRIER))){
		lightnessResponse(CLUNET_BROADCAST_ADDRESS);	//auto triggering
	}
}
*/


/*ISR(TIMER1_COMPB_vect){
	if (necReadSignal()){
		OCR1B += NEC_TIMER_CMP_TICKS;
	}else{
		DISABLE_TIMER_CMP_B;
	}
}
*/

//char shouldSendDelayedResponse = 0;
//unsigned int delayedResponseCounterValue = 0;

ISR(TIMER1_COMPA_vect){
	++systime;
		
	sei();	//разрешаем прерывания более высокого приоритета (clunet)
	
	char state = BUTTON_READ;
	if (state != buttonStateValue){
		buttonStateValue = state;
		
		if (buttonStateValue){
			buttonResponse(CLUNET_BROADCAST_ADDRESS);
			switchExecute(FAN_RELAY_ID, 0x02);	//toggle
		}
	}
	
	
	state = HALL_SENSOR_READ;
	if (state != hallSensorValue){
		hallSensorValue = state;
		exhaustedFanResponse(CLUNET_BROADCAST_ADDRESS);
			
		switchExecute(FAN_RELAY_ID, hallSensorValue);
	}
	
	//check motion
	//char value = MOTION_SENSOR_READ;
	//if (motionSensorValue != value){
	//	motionSensorValue = value;
	//	motionResponse(CLUNET_BROADCAST_ADDRESS);
	//}
	
	//check lightness
	//set_bit(ADCSRA, ADSC);
	
	//if (necCheckSignal()){
	//	OCR1B  = TCNT1 + NEC_TIMER_CMP_TICKS;
	//	ENABLE_TIMER_CMP_B;
	//}
	
	/*uint8_t nec_address;
	uint8_t nec_command;
	
	if (necValue(&nec_address, &nec_command)){
		if (nec_address == 0x02){
			char channel[2];
			channel[0] = 0;
			
			switch (nec_command){
				case 0x80:
					channel[1] = 0;
					clunet_send(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_CHANNEL, channel, sizeof(channel));
					necResetValue();
				break;
				case 0x40:
					channel[1] = 1;
					clunet_send(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_CHANNEL, channel, sizeof(channel));
					necResetValue();
				break;
				case 0xC0:
					channel[1] = 2;
					clunet_send(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_CHANNEL, channel, sizeof(channel));
					necResetValue();
				break;
				case 0x20:
					channel[1] = 3;
					clunet_send(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_CHANNEL, channel, sizeof(channel));
					necResetValue();
				break;
				case 0xC1:
				case 0xF8:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT_PREV);
					necResetValue();
				break;
				case 0x41:
				case 0xD8:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT_NEXT);
					necResetValue();
				break;
				
				case 0x08:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_MUTE_TOGGLE);
					necResetValue();
				break;
				case 0x58:
					shouldSendDelayedResponse = 1;
					delayedResponseCounterValue = TCNT1;
					cmd(0, CLUNET_BROADCAST_ADDRESS, COMMAND_VOLUME_UP, 2);
				break;
				case 0x78:
					shouldSendDelayedResponse = 1;
					delayedResponseCounterValue = TCNT1;
					cmd(0, CLUNET_BROADCAST_ADDRESS, COMMAND_VOLUME_DOWN, 2);
				break;
				case 0xA0:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_TREBLE_UP);
					necResetValue();
				break;
				case 0x10:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_TREBLE_DOWN);
					necResetValue();
				break;
				case 0x60:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_BASS_UP);
					necResetValue();
				break;
				case 0x90:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_BASS_DOWN);
					necResetValue();
				break;
				case 0x00:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_EQUALIZER_RESET);
					necResetValue();
				break;
			}
		}
	}

	
	//send delayed response
	//отправляем не раньше чем через 150 мс, но все равно рвется прерыванием и отправляется только по окончании любой длительной операции
	if (shouldSendDelayedResponse && (TCNT1 - delayedResponseCounterValue >= TIMER_SKIP_EVENTS_DELAY)){
		shouldSendDelayedResponse = 0;
		//cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_VOLUME_INFO);
		//also need to save to eeprom
	}
	*/
	OCR1A = TCNT1 + TIMER_NUM_TICKS;
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
		//case CLUNET_COMMAND_MOTION:
		//if (size == 0){
		//	motionResponse(src_address);
		//}
		//break;
		case CLUNET_COMMAND_TEMPERATURE:
		switch (size){
			case 1:
				if (data[0] == 0){
					temperatureResponse(src_address);
				}
			break;
			case 2:
				if (data[0] == 1 && data[1] == 1){
					temperatureResponse(src_address);
				}
			break;
			case 3:
				if (data[0] == 2 && data[1] == 1 && data[2] == DHT_SENSOR_ID){
					temperatureResponse(src_address);
				}
			break;
		}
		break;
		case CLUNET_COMMAND_HUMIDITY:
		if (size == 0){
			humidityResponse(src_address);
		}
		break;
		//case CLUNET_COMMAND_LIGHT_LEVEL:
		//if (size == 0){
		//	lightnessResponse(src_address);
		//}
		break;
		case CLUNET_COMMAND_BUTTON:
		if (size == 0){
			buttonResponse(src_address);
		}
		break;
		case CLUNET_COMMAND_DEVICE_STATE:
		if (size == 2){
			switch (data[0]){
				case EXHAUST_FAN_DEVICE_ID:
					if (data[1] == 0xFF){
						exhaustedFanResponse(src_address);
					}
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
	
	//MOTION_SENSOR_INIT;
	//motionSensorValue = MOTION_SENSOR_READ;
	
	//ADC_INIT;
	//lightnessSensorValue = -1;
	
	BUTTON_INIT;
	buttonStateValue = BUTTON_READ;
	
	HALL_SENSOR_INIT;
	hallSensorValue = HALL_SENSOR_READ;
	
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_init();
	
	
	while(1){
		
	}
	
	return 0;
}