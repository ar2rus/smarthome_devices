
#include "BathSensors.h"

char doorSensorValue;
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
	sei();
	
	char value_ = lightnessSensorValue;
	lightnessSensorValue = 100 * ADCH / 255;
	if (value_ >= 0 && ((lightnessSensorValue > LIGHTNESS_BARRIER) ^ (value_ > LIGHTNESS_BARRIER))){
		lightnessResponse(CLUNET_BROADCAST_ADDRESS);	//auto triggering
	}
}


ISR(TIMER1_COMPB_vect){
	if (necReadSignal()){
		OCR1B += NEC_TIMER_CMP_TICKS;
	}else{
		DISABLE_TIMER_CMP_B;
	}
}

char shouldSendDelayedResponse = 0;
unsigned int delayedResponseCounterValue = 0;

//не стоит проверять уровень освещенности чаще 5 мс,
//иначе появляется дребезг при переходе через 10%
char lightness_skip_cnt = 0;

ISR(TIMER1_COMPA_vect){
	++systime;
	
	sei();	//разрешаем прерывания более высокого приоритета (clunet)
	
	//check door
	char value = DOOR_SENSOR_READ;
	if (doorSensorValue != value){
		doorSensorValue = value;
		doorResponse(CLUNET_BROADCAST_ADDRESS);
	}
	
	//check lightness every 5 ms
	if (lightness_skip_cnt++==5){
		lightness_skip_cnt = 0;
		set_bit(ADCSRA, ADSC);
	}
	
	if (necCheckSignal()){
		OCR1B  = TCNT1 + NEC_TIMER_CMP_TICKS;
		ENABLE_TIMER_CMP_B;
	}
	
	uint8_t nec_address;
	uint8_t nec_command;
	
	if (necValue(&nec_address, &nec_command)){
		if (nec_address == 0x02){
			char channel[2];
			channel[0] = 0;
			
			//clunet_send(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_DOOR, channel, sizeof(channel));
			
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
				/*case 0xC1:
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
				break;*/
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

	OCR1A = TCNT1 + TIMER_NUM_TICKS;
}

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
	
	ADC_INIT;
	lightnessSensorValue = -1;
	
	clunet_init();
	clunet_set_on_data_received(clunet_data_received);
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	sei();
	
	while(1){
		
	}
	
	return 0;
}