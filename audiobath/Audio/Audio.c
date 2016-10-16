
#include "Audio.h"

void savePower(uint8_t on){
	eeprom_update_byte((void*)EEPROM_CONFIG_ADDRESS + offsetof(config, power), on);
}

void saveInput(uint8_t channel){
	eeprom_update_byte((void*)EEPROM_CONFIG_ADDRESS + offsetof(config, input), channel);
}

void saveVolume(uint8_t volume_dB){
	eeprom_update_byte((void*)EEPROM_CONFIG_ADDRESS + offsetof(config, volume), -volume_dB);
}

void saveEqualizer(uint8_t gain_db, int8_t treble_db, uint8_t bass_db){
	eeprom_update_byte((void*)EEPROM_CONFIG_ADDRESS + offsetof(config, eq_gain), gain_db);
	eeprom_update_byte((void*)EEPROM_CONFIG_ADDRESS + offsetof(config, eq_treble), treble_db);
	eeprom_update_byte((void*)EEPROM_CONFIG_ADDRESS + offsetof(config, eq_bass), bass_db);
}

void saveFMChannel(fm_channel_info* channel_info){
	eeprom_update_byte((void*)EEPROM_CONFIG_ADDRESS + offsetof(config, fm_channel), channel_info->channel);
	eeprom_update_word((void*)EEPROM_CONFIG_ADDRESS + offsetof(config, fm_freq), channel_info->frequency);
}

void saveFMControls(fm_state_info* state_info){
	eeprom_update_byte((void*)EEPROM_CONFIG_ADDRESS + offsetof(config, fm_controls), state_info->state);
}

char power_state;
void power(char on){
	power_state = on;
	
	if (on){
		lc75341_unmute();
	}else{
		lc75341_mute();
	}
	FM_control(FM_CONTROL_STANDBY, !on);
}


uint8_t delayedResponseAddress = -1;
int16_t delayedResponseCounterValue = -1;

ISR(TIMER_COMP_VECTOR){
	sei();
	
	if (delayedResponseCounterValue >= 0){
		if (--delayedResponseCounterValue < 0){
			char d = 0xFF;
			clunet_buffered_push(delayedResponseAddress, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_VOLUME, &d, 1);
		}
	}
	
	
	OCR1A = TCNT1 + TIMER_NUM_TICKS;
}


uint8_t check_power(){
	if (!power_state){
		power(1);
		return 5;
	}
	return 0;
}

void check_fm_channel(){
	if (power_state){
		FM_control(FM_CONTROL_MUTE, lc75341_input_value() != LC75341_INPUT_4);
	}
}

void cmd(clunet_msg* m){
	
		//в выключенном состоянии разрешаем только
		//CLUNET_COMMAND_POWER и CLUNET_COMMAND_CHANNEL
		if (!power_state){
			switch (m->command){
				case CLUNET_COMMAND_POWER:
				case CLUNET_COMMAND_CHANNEL:
				case CLUNET_COMMAND_RC_BUTTON_PRESSED:
				break;
				default:
				return;
			}
		}
		
		LED_ON;
		
		char response = 0;
		char silent = (m->src_address == CLUNET_DEVICE_ID);
		
		switch(m->command){
			
			// 		case CLUNET_COMMAND_DEBUG:{
			// 			config c;
			// 			eeprom_read_block(&c, (void*)EEPROM_CONFIG_ADDRESS, sizeof(c));
			// 			clunet_send_fairy(m->src_address,CLUNET_PRIORITY_COMMAND,CLUNET_COMMAND_DEBUG,&c,sizeof(c));
			// 		}
			// 		break;
			
			case CLUNET_COMMAND_POWER:
			if (m->size == 1){
				switch (m->data[0]){
					case 0:
					case 1:
						power(m->data[0]);
						response = 5;
					break;
					case 2:
						power(!power_state);
						response = 5;
					break;
					case 0xFF:
						response = 5;
						break;
				}
			}
			break;
			
			case CLUNET_COMMAND_RC_BUTTON_PRESSED:
				if (m->size == 3){
					if (m->data[0] == 0x00){		//nec
						if (m->data[1] == 0x00){	//address == 0, no repeats
						char data[2];
						switch(m->data[2]){
							case 0xA2:	//toggle power
								data[0] = 0x02;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_POWER, data, 1);
								break;
							case 0xE2:	//toggle mute
								data[0] = 0x01;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_MUTE, data, 1);
								break;
							case 0x22:	//pad channel
								data[0] = 0;
								data[1] = 1;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_CHANNEL, data, 2);
								break;
							case 0xC2:	//fm channel
								data[0] = 0;
								data[1] = 4;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_CHANNEL, data, 2);
								break;
							case 0x30:	//treble up
								data[0]=2;
								data[1]=2;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_EQUALIZER, data, 2);
								break;
							case 0x10:	//treble down
								data[0]=2;
								data[1]=3;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_EQUALIZER, data, 2);
								break;
							case 0x18:	//bass up
								data[0]=3;
								data[1]=2;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_EQUALIZER, data, 2);
								break;
							case 0x38:	//bass down
								data[0]=3;
								data[1]=3;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_EQUALIZER, data, 2);
								break;
							case 0x7A:	//gain up
								data[0]=1;
								data[1]=2;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_EQUALIZER, data, 2);
								break;
							case 0x5A:	//gain down
								data[0]=1;
								data[1]=3;
								clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_EQUALIZER, data, 2);
								break;
							case 0x02:	//next
								switch (lc75341_input_value()){
									case 0:{		//android tablet
										data[0] = 0x06;	//next
										clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, data, 1);
										break;
									}
									case 3:{		//fm tuner
										data[0] = 0x02;
										clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_FM, data, 1);
										break;
									}
								}
								break;
							case 0x98:	//prev
								switch (lc75341_input_value()){
									case 0:		//android tablet
										data[0] = 0x05;	//next
										clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, data, 1);
										break;
									case 3:		//fm tuner
										data[0] = 0x03;
										clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_FM, data, 1);
										break;
								}
								break;
							case 0xA8:	//ok
								switch (lc75341_input_value()){
									case 0:		//android tablet
										data[0] = 0x07;	//play/pause
										clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_ANDROID, data, 1);
										break;
									case 3:		//fm tuner
										break;
								}
								break;
							}
						}
					
						if ((m->data[1] & (~_BV(7))) == 0x00){	//address == 0, repeats allowed
							char data[2];
							switch(m->data[2]){
								case 0x90:	//volume up
									delayedResponseCounterValue = TIMER_SKIP_EVENTS_DELAY;
									data[0] = 2;
									clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_VOLUME, data, 1);
									break;
								case 0xE0:	//volume down
									delayedResponseCounterValue = TIMER_SKIP_EVENTS_DELAY;
									data[0] = 3;
									clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_VOLUME, data, 1);
									break;
							}
						}
					}
				}
			break;
			
			case CLUNET_COMMAND_CHANNEL:
			switch (m->size){
				case 1:
				switch (m->data[0]){
					case 0xFF:
					if (power_state){
						response = 1;
					}
					break;
					case 0x01:
					response = check_power();
					if (!response){
						lc75341_input_next();
					
						check_fm_channel();
						response = 1;
					}
					break;
					case 0x02:
					response = check_power();
					if (!response){
						lc75341_input_prev();
					
						check_fm_channel();
						response = 1;
					}
					break;
				}
				break;
				case 2:
				switch(m->data[0]){
					case 0x00:
					response = check_power();
					if (!response){
						lc75341_input(m->data[1] - 1);
					
						check_fm_channel();
						response = 1;
					}
					break;
				}
				break;
			}
			break;
			case CLUNET_COMMAND_VOLUME:
			switch (m->size){
				case 1:
				switch (m->data[0]){
					case 0xFF:
					response = 2;
					break;
					case 0x02:
					lc75341_volume_up_exp(2);
					response = 2;
					break;
					case 0x03:
					lc75341_volume_down_exp(2);
					response = 2;
					break;
				}
				break;
				case 2:
				switch(m->data[0]){
					case 0x00:
					lc75341_volume_percent(m->data[1]);
					response = 2;
					break;
					case 0x01:
					lc75341_volume_dB(m->data[1]);
					response = 2;
					break;
				}
				break;
			}
			break;
			case CLUNET_COMMAND_MUTE:
			if (m->size == 1){
				switch(m->data[0]){
					case 0:
					lc75341_volume_percent(0);
					response = 2;
					break;
					case 1:
					lc75341_mute_toggle();
					response = 2;
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
					response = 3;
					break;
					case 0xFF:
					response = 3;
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
						response = 3;
						break;
						case 0x01:	//dB
						if (m->size == 3){
							lc75341_gain_dB(m->data[2]);
							response = 3;
						}
						break;
						case 0x02:	//+
						lc75341_gain_up();
						response = 3;
						break;
						case 0x03:	//-
						lc75341_gain_down();
						response = 3;
						break;
					}
					break;
					
					case 0x02:	//treble
					switch(m->data[1]){
						case 0x00:	//reset
						lc75341_treble_dB(0);
						response = 3;
						break;
						case 0x01:	//dB
						if (m->size == 3){
							lc75341_treble_dB(m->data[2]);
							response = 3;
						}
						break;
						case 0x02:	//+
						lc75341_treble_up();
						response = 3;
						break;
						case 0x03:	//-
						lc75341_treble_down();
						response = 3;
						break;
					}
					break;
					
					case 0x03:	//bass
					switch(m->data[1]){
						case 0x00:	//reset
						lc75341_bass_dB(0);
						response = 3;
						break;
						case 0x01:	//dB
						if (m->size == 3){
							lc75341_bass_dB(m->data[2]);
							response = 3;
						}
						break;
						case 0x02:	//+
						lc75341_bass_up();
						response = 3;
						break;
						case 0x03:	//-
						lc75341_bass_down();
						response = 3;
						break;
					}
					break;
				}
				break;
			}
			break;
			case CLUNET_COMMAND_FM:
			if (m->size > 0){
				switch(m->data[0]){
					case 0xFF:	//info
					if (m->size == 2){
						switch (m->data[1]){
							case 0x00:
							response = 10;
							break;
							case 0x01:
							response = 11;
						}
					}
					break;
					case 0x00:	//freq
					if (m->size == 3){
						uint16_t* f = (uint16_t*)&m->data[1];
						if (FM_select_frequency(*f)){	//check band limit
							response = 10;
						}
					}
					break;
					case 0x01:	//saved channel
					if (m->size == 2){
						if (FM_select_channel(m->data[1])){
							response = 10;
						}
					}
					break;
					case 0x02:	//next saved
					case 0x03:	//prev saved
					if (m->size == 1){
						if (FM_select_next_channel(m->data[0] == 0x02)){
							response = 10;
						}
					}
					break;
					
					case 0x05:	//search
					response = 12;
					break;
					
					case 0x0A:
					if (m->size == 3){
						if (FM_control(m->data[1], m->data[2])){
							response = 11;
						}
					}
					break;
					
					case 0xEA:	//request num saved channels
					if (m->size == 1){
						m->data[1] = FM_get_num_channels();
						clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
					}
					break;
					case 0xEB:	//get saved channel's frequency
					if (m->size == 2){
						
						uint16_t* freq = (uint16_t*)(&m->data[2]);
						*freq = FM_get_channel_frequency(m->data[1]);
						clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 4);
					}
					break;
					case 0xEC:	//add channel
					switch(m->size){
						case 1:	{	//current freq
							fm_channel_info* info = FM_channel_info();
							m->data[1] = FM_add_channel(info->frequency);
							clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
						}
						break;
						case 3: {	//specified freq
							uint16_t* f = (uint16_t*)&m->data[1];
							m->data[1] = FM_add_channel(*f);
							clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
						}
						break;
					}
					break;
					case 0xED:	//save channel
					switch(m->size){
						case 2:	{	//current freq
							fm_channel_info* info = FM_channel_info();
							m->data[1] = FM_save_channel(m->data[1], info->frequency);
							clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
						}
						break;
						case 4:	{	//specified freq
							uint16_t* f = (uint16_t*)&m->data[2];
							m->data[1] = FM_save_channel(m->data[1], *f);
							clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
						}
						break;
					}
					break;
					case 0xEE:
					if (m->size == 3){
						if (m->data[1] == 0xEE && m->data[2] == 0xFF){
							FM_clear_channels();
							m->data[1] = 1;
							clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
							break;
						}
					}
					break;
				}
			}
			break;
		}
		
		if (!silent){
			switch(response){
				case 1:{
					char channel = lc75341_input_value() + 1;	//0 channel -> to 1 channel
					clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_CHANNEL_INFO, &channel, sizeof(channel));
					saveInput(channel-1);
				}
				break;
				case 2:{
					char data[2];
					data[0] = lc75341_volume_percent_value();
					data[1] = lc75341_volume_dB_value();
					clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_VOLUME_INFO, (char*)&data, sizeof(data));
					saveVolume(data[1]);
				}
				break;
				case 3:{
					char data[3];
					data[0] = lc75341_gain_dB_value();
					data[1] = lc75341_treble_dB_value();
					data[2] = lc75341_bass_dB_value();
					clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER_INFO, (char*)&data, sizeof(data));
					saveEqualizer(data[0] ,data[1] ,data[2]);
				}
				break;
				
				case 5:
				clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_POWER_INFO, (char*)&power_state, sizeof(power_state));
				savePower(power_state);
				break;
				
				case 10:{
					fm_channel_info* info = FM_channel_info();
					clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, (char*)info, sizeof(fm_channel_info));
					saveFMChannel(info);
				}
				break;
				case 11:{
					fm_state_info* info = FM_state_info();
					clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, (char*)info, sizeof(fm_state_info));
					saveFMControls(info);
				}
				break;
			}
		}
		LED_OFF;
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_CHANNEL:
		case CLUNET_COMMAND_VOLUME:
		case CLUNET_COMMAND_MUTE:
		case CLUNET_COMMAND_EQUALIZER:
		case CLUNET_COMMAND_FM:
		case CLUNET_COMMAND_POWER:
		case CLUNET_COMMAND_RC_BUTTON_PRESSED:
		//case CLUNET_COMMAND_DEBUG:
			clunet_buffered_push(src_address, dst_address, command, data, size);
			break;
	}
}

int main(void){
	cli();
	
	lc75341_init();
	lc75341_volume_percent(0);
	
	LED_INIT;
	TWI_INIT;
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_buffered_init();
	clunet_init();
	
	config c;
	eeprom_read_block(&c, (void*)EEPROM_CONFIG_ADDRESS, sizeof(c));
	
	
	if (!lc75341_input(c.input)){
		
		char data[2];
		
		data[0] = 0;
		data[1] = 1;
		clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_CHANNEL, data, 2);
	}
	
	lc75341_gain_dB(c.eq_gain);
	lc75341_treble_dB(c.eq_treble);
	lc75341_bass_dB(c.eq_bass);
	
	if (c.fm_channel >= 0){
		FM_select_channel(c.fm_channel);
		}else if (c.fm_freq > 0){
		FM_select_frequency(c.fm_freq);
	}
	
	for (int i=0; i<4; i++){
		FM_control(i, bit(c.fm_controls, i));
	}

	if (!lc75341_volume_dB(-c.volume)){
		//char data[2];
		//data[0] = 0x00;
		//data[1] = 30;		//30%
		//clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_VOLUME, data, 2);
		
		//muted by default
	}
	
	if (c.power < 0){
		//power on, send response and write to eeprom, by default
		char data = 1;
		clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_POWER, &data, sizeof(data));
	}else{
		power(c.power);
	}

	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	while (1){
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
	}
	return 0;
	
}

