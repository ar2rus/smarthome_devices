
#include "Audio.h"


volatile unsigned int systime = 0;

volatile char shouldSendDelayedResponse = 0;
volatile unsigned int delayedResponseCounterValue = 0;

void sendResponse(char response){
	//провер€ем, не отправл€етс€ ли уже чего-нибудь
	while (clunet_ready_to_send());
	
	switch (response){
		case RESPONSE_CHANNEL:{
			char channel = lc75341_input_value() + 1;	//0 channel -> to 1 channel
			clunet_send(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_CHANNEL_INFO, &channel, sizeof(channel));
			break;
		}
		case RESPONSE_VOLUME:{
			char data[2];
			data[0] = lc75341_volume_percent_value();
			data[1] = lc75341_volume_dB_value();
			clunet_send(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_VOLUME_INFO, (char*)&data, sizeof(data));
			break;
		}
		case RESPONSE_EQUALIZER:{
			char data[3];
			data[0] = lc75341_gain_dB_value();
			data[1] = lc75341_treble_dB_value();
			data[2] = lc75341_bass_dB_value();
			clunet_send(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER_INFO, (char*)&data, sizeof(data));
			break;
		}
	}
}

void cmd(clunet_msg* m){
	LED_ON;
	char response = -1;
	
	switch(m->command){
		
		case CLUNET_COMMAND_SWITCH:
			if (m->size == 1){
				switch (m->data[0]){
					case 0:
						break;
					case 1:
						break;
					case 2:
						break;
				}
			}
			break;
		
		case CLUNET_COMMAND_CHANNEL:
		switch (m->size){
			case 1:
			switch (m->data[0]){
				case 0xFF:
					response = RESPONSE_CHANNEL;
					break;
				case 0x01:
					lc75341_input_next();
					response = RESPONSE_CHANNEL;
					break;
				case 0x02:
					lc75341_input_prev();
					response = RESPONSE_CHANNEL;
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
		break;
		
		case CLUNET_COMMAND_VOLUME:
		switch (m->size){
			case 1:
			switch (m->data[0]){
				case 0xFF:
					response = RESPONSE_VOLUME;
					break;
				case 0x02:
					shouldSendDelayedResponse = 1;
					delayedResponseCounterValue = systime;
					lc75341_volume_up(2);
					//response = RESPONSE_VOLUME;
					break;
				case 0x03:
					shouldSendDelayedResponse = 1;
					delayedResponseCounterValue = systime;
					lc75341_volume_down(2);
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
					lc75341_reset_equalizer();
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
	
	sendResponse(response);
	
	LED_OFF;
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_SWITCH:
		case CLUNET_COMMAND_CHANNEL:
		case CLUNET_COMMAND_VOLUME:
		case CLUNET_COMMAND_MUTE:
		case CLUNET_COMMAND_EQUALIZER:
			clunet_buffered_push(src_address, dst_address, command, data, size);
	}
}

ISR(TIMER1_COMPA_vect){
	++systime;
}

int main(void){
	cli();
	
	LED_INIT;
	TWI_INIT;
		
	tea5767_init();
	lc75341_init();
	
	tea5767_set_LO_PLL(99.1);
	lc75341_volume_percent(50);

	clunet_set_on_data_received(clunet_data_received);
	clunet_buffered_init();
	clunet_init();
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	while (1){
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
		
		//отправл€ем отложенный ответ дл€ повтор€ющихс€ команд
		//отправл€ем при паузе между одинаковыми командами более чем TIMER_SKIP_EVENTS_DELAY мс
		if (shouldSendDelayedResponse && (systime - delayedResponseCounterValue >= TIMER_SKIP_EVENTS_DELAY)){
			shouldSendDelayedResponse = 0;
			sendResponse(RESPONSE_VOLUME);
			//also need to save to eeprom
		}
	}
	return 0;
	
}

