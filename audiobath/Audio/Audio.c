
#include "Audio.h"


volatile unsigned int systime = 0;

volatile signed   int delayedResponseAddress = -1;
volatile unsigned int delayedResponseCounterValue = 0;

volatile unsigned char powerState = 1;	//флаг вкл/выкл


void sendResponse(unsigned char address, char response){
	switch (response){
		case RESPONSE_POWER:{
			clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_POWER_INFO, (char*)&powerState, sizeof(powerState));
			break;
		}
		case RESPONSE_CHANNEL:{
			char channel = lc75341_input_value() + 1;	//0 channel -> to 1 channel
			clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_CHANNEL_INFO, &channel, sizeof(channel));
			break;
		}
		case RESPONSE_VOLUME:{
			char data[2];
			data[0] = lc75341_volume_percent_value();
			data[1] = lc75341_volume_dB_value();
			clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_VOLUME_INFO, (char*)&data, sizeof(data));
			break;
		}
		case RESPONSE_EQUALIZER:{
			char data[3];
			data[0] = lc75341_gain_dB_value();
			data[1] = lc75341_treble_dB_value();
			data[2] = lc75341_bass_dB_value();
			clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER_INFO, (char*)&data, sizeof(data));
			break;
		}
	}
}

void power(unsigned char on_){
	if (powerState !=  on_){
		if (on_){
			lc75341_eeprom_load();
			//tea_poweron
		}else{
			lc75341_eeprom_enable(0);  // выключаем сохранение в eeprom
			lc75341_mute();			   // отключаем звук
			lc75341_input(2);		   // переключаем на неиспользуемый канал, дл€ полного устранени€ звука
			lc75341_eeprom_enable(1);  // теперь дальше можно писать в eeprom
			//tea_poweroff
		}
		powerState = on_;
		eeprom_write_byte((void*)EEPROM_ADDRESS_AUDIOBATH_POWER, powerState);
	}
}

void cmd(clunet_msg* m){
	LED_ON;
	char response = -1;
	
	switch(m->command){
		
		case CLUNET_COMMAND_POWER:
			if (m->size == 1){
				switch (m->data[0]){
					case 0:
					case 1:
						power(m->data[0]);
						response = RESPONSE_POWER;
						break;
					case 2:
						power(!powerState);
						response = RESPONSE_POWER;
						break;
					case 0xFF:
						response = RESPONSE_POWER;
						break;
				}
			}
			break;
		
		case CLUNET_COMMAND_CHANNEL:
		if (powerState){
			switch (m->size){
				case 1:
				switch (m->data[0]){
					case 0xFF:
						response = RESPONSE_CHANNEL;
						break;
					case 0x01:
						switch (lc75341_input_value()){
							case 0:{		//android tablet
								char data = 0x06;	//next
								clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, &data, 1);
								break;
							}
							case 3:{		//fm tuner
								break;
								//do here tea next channel searching
							}
						}
						break;
					case 0x02:
						switch (lc75341_input_value()){
							case 0:{		//android tablet
								char data = 0x05;	//prev
								clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, &data, 1);
								break;
							}
							case 3:{		//fm tuner
								break;
								//do here tea prev channel searching
							}
						}
						break;
				}
				break;
				case 2:
				switch(m->data[0]){
					case 0x00:
						lc75341_input(m->data[1] - 1);	//1 channel -> 0 channel, etc
						response = RESPONSE_CHANNEL;
						break;
				}
				break;
			}
		}
		break;
		
		case CLUNET_COMMAND_VOLUME:
		if (powerState){
			switch (m->size){
				case 1:
				switch (m->data[0]){
					case 0xFF:
						response = RESPONSE_VOLUME;
						break;
					case 0x02:
						lc75341_eeprom_enable(0);  // отрубаем запись в eeprom дл€ многократно повтор€ющихс€ команд
						lc75341_volume_up_exp(2);
					
						delayedResponseAddress = m->src_address;
						delayedResponseCounterValue = systime;
						//response = RESPONSE_VOLUME;
						break;
					case 0x03:
						lc75341_eeprom_enable(0);  // отрубаем запись в eeprom дл€ многократно повтор€ющихс€ команд
						lc75341_volume_down_exp(2);
					
						delayedResponseAddress = m->src_address;
						delayedResponseCounterValue = systime;
						//response = RESPONSE_VOLUME;
						break;
				}
				break;
				case 2:
				switch(m->data[0]){
					case 0x00:
						lc75341_volume_percent(m->data[1]);
						response = RESPONSE_VOLUME;
						break;
					case 0x01:
						lc75341_volume_dB(m->data[1]);
						response = RESPONSE_VOLUME;
						break;
				}
				break;
			}
		}
		break;
		
		case CLUNET_COMMAND_MUTE:
		if (m->size == 1){
			switch(m->data[0]){
				case 0:
					lc75341_volume_percent(0);
					response = RESPONSE_VOLUME;
					break;
				case 1:
					lc75341_mute_toggle();
					response = RESPONSE_VOLUME;
					break;
			}
		}
		break;
		
		case CLUNET_COMMAND_EQUALIZER:
			switch(m->size){
				case 1:
				switch(m->data[0]){
					case 0x00:
						lc75341_equalizer_reset();
						response = RESPONSE_EQUALIZER;
						break;
					case 0xFF:
						response = RESPONSE_EQUALIZER;
						break;
				}
				break;
				case 2:
				case 3:
				switch(m->data[0]){
					case 0x01:	//gain
					switch(m->data[1]){
						case 0x00:	//reset
							lc75341_gain_dB(0);
							response = RESPONSE_EQUALIZER;
							break;
						case 0x01:	//dB
							if (m->size == 3){
								lc75341_gain_dB(m->data[2]);
								response = RESPONSE_EQUALIZER;
							}
							break;
						case 0x02:	//+
							lc75341_gain_up();
							response = RESPONSE_EQUALIZER;
							break;
						case 0x03:	//-
							lc75341_gain_down();
							response = RESPONSE_EQUALIZER;
							break;
					}
					break;
					case 0x02:	//treble
					switch(m->data[1]){
							case 0x00:	//reset
							lc75341_treble_dB(0);
							response = RESPONSE_EQUALIZER;
							break;
						case 0x01:	//dB
							if (m->size == 3){
								lc75341_treble_dB(m->data[2]);
								response = RESPONSE_EQUALIZER;
							}
							break;
						case 0x02:	//+
							lc75341_treble_up();
							response = RESPONSE_EQUALIZER;
							break;
						case 0x03:	//-
							lc75341_treble_down();
							response = RESPONSE_EQUALIZER;
							break;
					}
					break;
					case 0x03:	//bass
					switch(m->data[1]){
						case 0x00:	//reset
							lc75341_bass_dB(0);
							response = RESPONSE_EQUALIZER;
							break;
						case 0x01:	//dB
							if (m->size == 3){
								lc75341_bass_dB(m->data[2]);
								response = RESPONSE_EQUALIZER;
							}
							break;
						case 0x02:	//+
							lc75341_bass_up();
							response = RESPONSE_EQUALIZER;
							break;
						case 0x03:	//-
							lc75341_bass_down();
							response = RESPONSE_EQUALIZER;
							break;
					}
					break;
				}
				break;
			}
		break;
	}
	
	sendResponse(m->src_address, response);
	
	LED_OFF;
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, 
							unsigned char command, char* data, unsigned char size){
		switch(command){
			case CLUNET_COMMAND_CHANNEL:
			case CLUNET_COMMAND_VOLUME:
			case CLUNET_COMMAND_MUTE:
			case CLUNET_COMMAND_EQUALIZER:
				if (!powerState) break;	//игнорим все запросы при выключенном девайсе
			case CLUNET_COMMAND_POWER:
				clunet_buffered_push(src_address, dst_address, command, data, size);
		}
}

ISR(TIMER_COMP_VECTOR){
	++systime;
	
	TIMER_REG = 0;	//reset counter
}

int main(void){
	cli();
	
	LED_INIT;
	TWI_INIT;
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_buffered_init();
	clunet_init();
	
	tea5767_init();
	lc75341_init();
	
	unsigned char state = eeprom_read_byte((void*)EEPROM_ADDRESS_AUDIOBATH_POWER);	//читаем сохраненное состо€ние (вкл/выкл)
	powerState = !state;
	power(state);
	sendResponse(CLUNET_BROADCAST_ADDRESS, RESPONSE_POWER);
	
	tea5767_set_LO_PLL(99.1);

	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	while (1){
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
		
		//отправл€ем отложенный ответ дл€ повтор€ющихс€ команд
		//отправл€ем при паузе между одинаковыми командами более чем TIMER_SKIP_EVENTS_DELAY мс
		if ((delayedResponseAddress >=0) && (systime - delayedResponseCounterValue >= TIMER_SKIP_EVENTS_DELAY)){
			
			unsigned char address = delayedResponseAddress;
			delayedResponseAddress = -1;
			
			lc75341_eeprom_enable(1);
			lc75341_eeprom_flush();
			
			sendResponse(address, RESPONSE_VOLUME);
		}
	}
	return 0;
	
}

