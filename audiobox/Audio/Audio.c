
#include "Audio.h"


ISR(TIMER1_COMPB_vect){
	if (necReadSignal()){
		OCR1B += NEC_TIMER_CMP_TICKS;
	}else{
		DISABLE_TIMER_CMP_B;
	}
}

char encoderValue = 0;

signed char readEncoder(){
	char newValue = bit(INPORT(ENCODER_1_PORT), ENCODER_1_PIN) | (bit(INPORT(ENCODER_2_PORT), ENCODER_2_PIN) << 1);
	signed char value = 0;
	switch(encoderValue){
		case 2: {
			if(newValue == 3) value = -1;
			else
			if(newValue == 0) value =  1;
			break;
		}
		case 0:{
			if(newValue == 2) value = -1;
			else
			if(newValue == 1) value =  1;
			break;
		}
		case 1:{
			if(newValue == 0) value = -1;
			else
			if(newValue == 3) value =  1;
			break;
		}
		case 3:{
			if(newValue == 1) value = -1;
			else
			if(newValue == 2) value =  1;
			break;
		}
	}
	
	encoderValue = newValue;
	return value;
}


char shouldSendDelayedResponse = 0;
unsigned int delayedResponseCounterValue = 0;

//только для предотвращения многократного вызова при удерживании кнопки нажатой
char buttonStates = 0;


void channel(uint8_t button){
	char data[2];
	
	data[0] = 0;
	data[1] = button;
	clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_CHANNEL, data, 2);
}

void button_channel(uint8_t button){
	if (!test_bit(buttonStates, button-1)){
		buttonStates = _BV(button-1);
		
		channel(button);
	}
}

ISR(TIMER1_COMPA_vect){
	char data[2];
	
	if (BUTTON1_READ){
		button_channel(1);
	}else
	if (BUTTON2_READ){
		button_channel(2);
	}else
	if (BUTTON3_READ){
		button_channel(3);
	}else
	if (BUTTON4_READ){
		button_channel(4);
	}else
	if (BUTTON_L_READ){
		if (!test_bit(buttonStates, 4)){
			buttonStates = _BV(4);
			FM_select_next_channel(0);
		}
	}else
	if (BUTTON_R_READ){
		if (!test_bit(buttonStates, 5)){
			buttonStates = _BV(5);
			FM_select_next_channel(1);
		}
	}else{
		buttonStates = 0;
	}

	signed char a = readEncoder();
	if (a > 0){
		shouldSendDelayedResponse = 1;
		delayedResponseCounterValue = TCNT1;
		
		data[0] = 2;
		//src_address == CLUNET_DEVICE_ID -> silent
		clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_VOLUME, data, 1);
	}else if (a < 0){
		shouldSendDelayedResponse = 1;
		delayedResponseCounterValue = TCNT1;
		
		data[0] = 3;
		//src_address == CLUNET_DEVICE_ID -> silent
		clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_VOLUME, data, 1);
	}
	
	if (necCheckSignal()){
		OCR1B  = TCNT1 + NEC_TIMER_CMP_TICKS;
		ENABLE_TIMER_CMP_B;
	}
	
	uint8_t nec_address;
	uint8_t nec_command;
	
	if (necValue(&nec_address, &nec_command)){
		
		if (nec_address == 0x02){
			switch (nec_command){
				case 0x80:
					channel(1);
					necResetValue();
					break;
				case 0x40:
					channel(2);
					necResetValue();
					break;
				case 0xC0:
					channel(3);
					necResetValue();
					break;
				case 0x20:
					channel(4);
					necResetValue();
					break;
				case 0xC1:
				case 0xF8:
					data[0] = 2;
					clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_CHANNEL, data, 1);
					necResetValue();
					break;
				case 0x41:
				case 0xD8:
					data[0] = 1;
					clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_CHANNEL, data, 1);
					necResetValue();
					break;
					
				case 0x08:
					data[0] = 1;
					clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_MUTE, data, 1);
					//cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_MUTE_TOGGLE);
					necResetValue();
					break;
				case 0x58:
					shouldSendDelayedResponse = 1;
					delayedResponseCounterValue = TCNT1;
					data[0] = 2;
					clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_DEVICE_ID, CLUNET_COMMAND_VOLUME, data, 1);
					//cmd(0, CLUNET_BROADCAST_ADDRESS, COMMAND_VOLUME_UP, 2);
					break;
				case 0x78:
					shouldSendDelayedResponse = 1;
					delayedResponseCounterValue = TCNT1;
					data[0] = 3;
					clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_DEVICE_ID, CLUNET_COMMAND_VOLUME, data, 1);
					//cmd(0, CLUNET_BROADCAST_ADDRESS, COMMAND_VOLUME_DOWN, 2);
					break;
				case 0xA0:
					data[0] = 2;
					data[1] = 2;
					clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_DEVICE_ID, CLUNET_COMMAND_EQUALIZER, data, 2);
					
					//cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_TREBLE_UP);
					necResetValue();
					break;
				case 0x10:
					data[0] = 2;
					data[1] = 3;
					clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_DEVICE_ID, CLUNET_COMMAND_EQUALIZER, data, 2);
				
					//cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_TREBLE_DOWN);
					necResetValue();
					break;
				case 0x60:
					//FM_select_next_channel(0);
					necResetValue();
					break;
				case 0x90:
					//FM_select_next_channel(1);
					necResetValue();
					break;
				case 0x00:
					data[0] = 0;
					clunet_buffered_push(CLUNET_DEVICE_ID, CLUNET_DEVICE_ID, CLUNET_COMMAND_EQUALIZER, data, 1);
					//cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_EQUALIZER_RESET);
					necResetValue();
					break;
			}
		}
// 		else{
// 			char t[2];
// 			t[0] = nec_address;
// 			t[1] = nec_command;
// 			clunet_send_fairy(255,1,0x99,&t[0],2);
// 		}
	}

	
	//send delayed response
	//отправляем не раньше чем через 150 мс, но все равно рвется прерыванием 
	//и отправляется только по окончании любой длительной операции
	if (shouldSendDelayedResponse && (TCNT1 - delayedResponseCounterValue >= TIMER_SKIP_EVENTS_DELAY)){
		shouldSendDelayedResponse = 0;
		
		data[0] = 0xFF;
		clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_VOLUME, data, 1);
	}
	
	
	OCR1A = TCNT1 + TIMER_NUM_TICKS;
}

void cmd(clunet_msg* m){
	LED_ON;
	
	char response = 0;
	char silent = (m->src_address == CLUNET_DEVICE_ID);
	
	switch(m->command){
		case CLUNET_COMMAND_CHANNEL:
		switch (m->size){
			case 1:
			switch (m->data[0]){
				case 0xFF:
					response = 1;
					break;
				case 0x01:
					lc75341_input_next();
					response = 1;
					break;
				case 0x02:
					lc75341_input_prev();
					response = 1;
					break;
			}
			break;
			case 2:
			switch(m->data[0]){
				case 0x00:
					lc75341_input(m->data[1] - 1);
					response = 1;
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
				case 0x00:	//power off
				case 0x01:	//power on
					if (m->size == 1){
						FM_power(m->data[0]);
					}
					response = 10;
					break;
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
				case 0x02:	//freq
					if (m->size == 3){
						FM_select_frequency((uint16_t)(&m->data[1]));
						response = 10;
					}
					break;
				case 0x03:	//saved channel
					if (m->size == 2){
						if (FM_select_channel(m->data[1])){
							response = 10;
						}
					}
					break;
				case 0x04:	//next saved
				case 0x05:	//next prev
				if (m->size == 1){
					if (FM_select_next_channel(m->data[1] == 0x04)){
						response = 10;
					}
				}
				break;
				
				case 0x07:	//search
					response = 11;
					break;
				
				case 0x0A:
					if (m->size == 3){
						switch(m->data[1]){
							case 0x00:	//standby
								break;
							case 0x01:	//mono
								break;
							case 0x02:	//mute
								break;
							case 0x03:	//hcc
								break;	
							case 0x04:	//snc
								break;
						}
						response = 10;	//?
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
						case 1:	//current freq
							//data[1] = FM_add_channel(cur_freq);
							clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
							break;
						case 3:	//specified freq
							m->data[1] = FM_add_channel((uint16_t)(&m->data[1]));
							clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
							break;
					}
					break;
				case 0xED:	//save channel
					switch(m->size){
						case 2:	//current freq
							//data[1] = FM_add_channel(m->data[1], cur_freq);
							clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
							break;
						case 4:	//specified freq
							m->data[1] = FM_save_channel(m->data[1], (uint16_t)(&m->data[2]));
							clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_FM_INFO, m->data, 2);
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
			}
			break;
			case 2:{
				char data[2];
				data[0] = lc75341_volume_percent_value();
				data[1] = lc75341_volume_dB_value();
				clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_VOLUME_INFO, (char*)&data, sizeof(data));
			}
			break;
			case 3:{
				char data[3];
				data[0] = lc75341_gain_dB_value();
				data[1] = lc75341_treble_dB_value();
				data[2] = lc75341_bass_dB_value();
				clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER_INFO, (char*)&data, sizeof(data));
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
			clunet_buffered_push(src_address, dst_address, command, data, size);
	}
}

int main(void){
	cli();
	LED_INIT;
	BUTTONS_INIT;
	
	TWI_INIT;
	
	FM_select_frequency(9290);
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_buffered_init();
	clunet_init();
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	lc75341_init();
	lc75341_volume_percent(80);
	
	while (1){
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
	}
	return 0;
}

