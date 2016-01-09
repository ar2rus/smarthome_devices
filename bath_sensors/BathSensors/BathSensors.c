#include "BathSensors.h"

char doorSensorValue;

char shouldSendLightnessValue;
char lightnessSensorValue;


volatile uint32_t systime = 0;

void doorResponse(unsigned char address){
	char data = doorSensorValue;
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_DOOR_INFO, &data, sizeof(data));
}

void lightnessResponse(unsigned char address){
	char data[2];
	data[0] = lightnessSensorValue > LIGHTNESS_BARRIER;
	data[1] = lightnessSensorValue;
			
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_LIGHT_LEVEL_INFO, data, sizeof(data));
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

//lightness
ISR(ADC_vect){
	if (!shouldSendLightnessValue){
		char value_ = lightnessSensorValue;
		lightnessSensorValue = 100 * ADCH / 255;
		if (value_ >= 0 && ((lightnessSensorValue > LIGHTNESS_BARRIER) ^ (value_ > LIGHTNESS_BARRIER))){
			shouldSendLightnessValue = 1;
		}
	}
}


ISR(TIMER1_COMPB_vect){
	if (necReadSignal()){
		OCR1B += NEC_TIMER_CMP_TICKS;
	}else{
		DISABLE_TIMER_CMP_B;
	}
}

//не стоит проверять уровень освещенности чаще 5 мс,
//иначе появляется дребезг при переходе через 10%
char lightness_skip_cnt = 0;

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_DOOR:
		if (size == 0){
			doorResponse(src_address);
		}
		break;
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
		case CLUNET_COMMAND_LIGHT_LEVEL:
		if (size == 0){
			lightnessResponse(src_address);
		}
		break;
	}
}

int main(void){
	
	cli();
	
	DOOR_SENSOR_INIT;
	doorSensorValue = DOOR_SENSOR_READ;
	
	shouldSendLightnessValue = 0;
	lightnessSensorValue = -1;
	ADC_INIT;
	
	clunet_init();
	clunet_set_on_data_received(clunet_data_received);
	
	//TIMER_INIT;
	//ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	sei();
	
	while(1){
			++systime;
			
			//check door
			char value = DOOR_SENSOR_READ;
			if (doorSensorValue != value){
				doorSensorValue = value;
				doorResponse(CLUNET_BROADCAST_ADDRESS);
			}
			
			if (shouldSendLightnessValue){
				lightnessResponse(CLUNET_BROADCAST_ADDRESS);
				shouldSendLightnessValue = 0;
			}
			
			//check lightness every 100 ms
			if (lightness_skip_cnt++==100){
				lightness_skip_cnt = 0;
				set_bit(ADCSRA, ADSC);
			}
			
			if (necCheckSignal()){
				TIMER_INIT;
				OCR1B  = TCNT1 + NEC_TIMER_CMP_TICKS;
				ENABLE_TIMER_CMP_B;
			}
			
			uint8_t nec_address;
			uint8_t nec_command;
			
			if (necValue(&nec_address, &nec_command)){
				if (nec_address == 0x00){
					
					char data[2];
					
					switch (nec_command){
						case 0xA2:{		//switch on/off
							necResetValue();
						}
						break;
						case 0xE2:{		//toggle mute
							data[0] = 1;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_MUTE, data, 1);
							necResetValue();
						}
						break;
						case 0x22:{		//input_1
							data[0] = 0;
							data[1] = 1;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_CHANNEL, data, 2);
							necResetValue();
						}
						break;
						case 0xC2:{		//input_4 (fm)
							data[0] = 0;
							data[1] = 4;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_CHANNEL, data, 2);
							necResetValue();
						}
						break;
						
						case 0xE0:{		//volume down
							data[0] = 3;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_VOLUME, data, 1);
							//necResetValue();
						}
						break;
						
						case 0x90:{		//volume up
							data[0] = 2;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_VOLUME, data, 1);
							//necResetValue();
						}
						break;
						
						case 0x02:{		//next
							data[0] = 0x06;
							clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, data, 1);
							necResetValue();
						}
						break;
						case 0x98:{		//prev
							data[0] = 0x05;
							clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, data, 1);
							necResetValue();
						}
						break;
						case 0xA8:{		//ok
							data[0] = 0x07;
							clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, data, 1);
							necResetValue();
						}
						break;
						
						case 0x30:{		//treble up
							data[0]=2;
							data[1]=2;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER, data, 2);
							necResetValue();
						}
						break;
						case 0x10:{		//treble down
							data[0]=2;
							data[1]=3;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER, data, 2);
							necResetValue();
						}
						break;
						case 0x18:{		//bass up
							data[0]=3;
							data[1]=2;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER, data, 2);
							necResetValue();
						}
						break;
						case 0x38:{		//bass down
							data[0]=3;
							data[1]=3;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER, data, 2);
							necResetValue();
						}
						break;
						case 0x7A:{		//gain up
							data[0]=1;
							data[1]=2;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER, data, 2);
							necResetValue();
						}
						break;
						case 0x5A:{		//gain down
							data[0]=1;
							data[1]=3;
							clunet_send_fairy(AUDIOBATH_DEVICE_ID, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER, data, 2);
							necResetValue();
						}
						break;
						
						
						case 0x42:{	//lock/unlock android pad
							data[0] = 0x02;
							clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, data, 1);
							necResetValue();
						}
						break;
						case 0x52:{	//activate smarthome app on android pad
							data[0] = 0x0A;
							clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, data, 1);
							necResetValue();
						}
						break;
						case 0x4A:{	//bath fan control button
							data[0] = 0;
							clunet_send_fairy(FAN_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_FAN, data, 1);
							necResetValue();
						}
						break;
					}
				}
			}
			_delay_ms(1);
	}
	
	return 0;
}