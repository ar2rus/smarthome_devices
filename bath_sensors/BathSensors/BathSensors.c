#include "BathSensors.h"

char doorSensorValue;

char shouldSendLightnessValue;
char lightnessSensorValue;

volatile unsigned int systime = 0;
volatile unsigned int sensor_check_time = 0;

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

void rcResponse(unsigned char address, uint8_t nec_address, uint8_t nec_command, uint8_t nec_repeated){
	char data[3];
	data[0] = 0x00;
	data[1] = nec_address;
	if (nec_repeated){
		data[1] |= _BV(7);
	}
	data[2] = nec_command;
			
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_RC_BUTTON_PRESSED, data, sizeof(data));
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

ISR(TIMER_COMP_A_VECTOR){
	++systime;
	
	OCR1A += TIMER_NUM_TICKS;
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
		
		case CLUNET_COMMAND_DOOR:
			if (m->size == 0){
				doorResponse(m->src_address);
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
			
		case CLUNET_COMMAND_LIGHT_LEVEL:
			if (m->size == 0){
				lightnessResponse(m->src_address);
			}
			break;	
		
// 		//for debugging only
// 		case CLUNET_COMMAND_DEBUG:
// 			if (m->src_address == 0){	//supradin
// 					clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_DEBUG, m->data, 4);
// 			}
// 			break;
	}
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_DOOR:
		case CLUNET_COMMAND_TEMPERATURE:
		case CLUNET_COMMAND_HUMIDITY:
		case CLUNET_COMMAND_LIGHT_LEVEL:
			clunet_buffered_push(src_address, dst_address, command, data, size);
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
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_buffered_init();
	clunet_init();
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	while(1){
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
			
		//check sensors every 100 ms
		if (systime - sensor_check_time >= 100){
			sensor_check_time = systime;
			
			//check door
			char value = DOOR_SENSOR_READ;
			if (doorSensorValue != value){
				doorSensorValue = value;
				doorResponse(CLUNET_BROADCAST_ADDRESS);
			}
			
			//check lightness
			set_bit(ADCSRA, ADSC);
		}
		
		if (shouldSendLightnessValue){
			shouldSendLightnessValue = 0;
			lightnessResponse(CLUNET_BROADCAST_ADDRESS);
		}
			
		if (necCheckSignal()){
			OCR1B  = TCNT1 + NEC_TIMER_CMP_TICKS;
			ENABLE_TIMER_CMP_B;
		}
			
		uint8_t nec_address;
		uint8_t nec_command;
		uint8_t nec_repeated;
			
		if (necValue(&nec_address, &nec_command, &nec_repeated)){
			if (nec_command){
				rcResponse(CLUNET_BROADCAST_ADDRESS, nec_address, nec_command, nec_repeated);
			}
		}
	}
	
	return 0;
}