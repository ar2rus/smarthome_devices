
#include "lc75341/lc75341.h"
#include "tea5767/tea5767.h"

#include "clunet/clunet.h"

#include "Audio.h"


#include <stdarg.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <util/delay.h>


/*
*	Выполняет команду и, при необходимости, отправляет 
*	информационное сообщение в сеть CLUNET
*/
void cmd(uint8_t sendResponse, uint8_t responseAddress, const uint8_t command, ...){
	LED_ON;
	
	va_list va;
	va_start(va, command);
	
	char responseType = -1;
	
	switch (command){
		case COMMAND_INPUT:
			lc75341_input(va_arg(va, int));
			responseType = 0;
			break;
		case COMMAND_INPUT_NEXT:
			lc75341_input_next();
			responseType = 0;
			break;
		case COMMAND_INPUT_PREV:
			lc75341_input_prev();
			responseType = 0;
			break;
		case COMMAND_INPUT_INFO:
			responseType = 0;
			break;
			
		case COMMAND_MUTE_TOGGLE:
			lc75341_mute_toggle();
			responseType = 1;
			break;
		case COMMAND_VOLUME_DB:
			lc75341_volume_dB(va_arg(va, int));
			responseType = 1;
			break;
		case COMMAND_VOLUME_PCNT:
			lc75341_volume_percent(va_arg(va, int));
			responseType = 1;
			break;
		case COMMAND_VOLUME_UP:
			lc75341_volume_up_exp(va_arg(va, int));
			responseType = 1;
			break;
		case COMMAND_VOLUME_DOWN:
			lc75341_volume_down_exp(va_arg(va, int));
			responseType = 1;
			break;
		case COMMAND_VOLUME_INFO:
			responseType = 1;
			break;
		
			
		case COMMAND_TREBLE_DB:
			lc75341_treble_dB(va_arg(va, int));
			responseType = 2;
			break;
		case COMMAND_TREBLE_UP:
			lc75341_treble_up();
			responseType = 2;
			break;
		case COMMAND_TREBLE_DOWN:
			lc75341_treble_down();
			responseType = 2;
			break;
			
		case COMMAND_BASS_DB:
			lc75341_bass_dB(va_arg(va, int));
			responseType = 2;
			break;
		case COMMAND_BASS_UP:
			lc75341_bass_up();
			responseType = 2;
			break;
		case COMMAND_BASS_DOWN:
			lc75341_bass_down();
			responseType = 2;
			break;
		
		case COMMAND_GAIN_DB:
			lc75341_gain_dB(va_arg(va, int));
			responseType = 2;
			break;
		case COMMAND_GAIN_UP:
			lc75341_gain_up();
			responseType = 2;
			break;
		case COMMAND_GAIN_DOWN:
			lc75341_gain_down();
			responseType = 2;
			break;
		
		case COMMAND_EQUALIZER_RESET:
			lc75341_reset_equaliuzer();
			responseType = 2;
			break;
		case COMMAND_EQUALIZER_INFO:
			responseType = 2;
			break;
	}
	va_end(va);
	LED_OFF;
	
	sei();
	if (sendResponse){
		switch (responseType){
			case 0:{
				char channel = lc75341_input_value() + 1;	//0 channel -> to 1 channel
				clunet_send(responseAddress, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_CHANNEL_INFO, &channel, sizeof(channel));
				break;
			}
			case 1:{
				char data[2];
				data[0] = lc75341_volume_percent_value();
				data[1] = lc75341_volume_dB_value();
				clunet_send(responseAddress, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_VOLUME_INFO, (char*)&data, sizeof(data));
				break;
			}
			case 2:{
				char data[3];
				data[0] = lc75341_gain_dB_value();
				data[1] = lc75341_treble_dB_value();
				data[2] = lc75341_bass_dB_value();
				clunet_send(responseAddress, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER_INFO, (char*)&data, sizeof(data));
				break;
			}
		}
	}
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_CHANNEL:
			switch (size){
				case 1:
					switch (data[0]){
						case 0xFF:
							cmd(1, src_address, COMMAND_INPUT_INFO);
							break;
						case 0x01:
							cmd(1, src_address, COMMAND_INPUT_NEXT);
							break;
						case 0x02:
							cmd(1, src_address, COMMAND_INPUT_PREV);
							break;
					}
					break;
				case 2:
					switch(data[0]){
						case 0x00:
							cmd(1, src_address, COMMAND_INPUT, data[1] - 1);	//1 channel -> 0 channel
							break;
					}
					break;
			}
			break;
		case CLUNET_COMMAND_VOLUME:
			switch (size){
				case 1:
					switch (data[0]){
						case 0xFF:
							cmd(1, src_address, COMMAND_VOLUME_INFO);
							break;
						case 0x02:
							cmd(1, src_address, COMMAND_VOLUME_UP, 3);
							break;
						case 0x03:
							cmd(1, src_address, COMMAND_VOLUME_DOWN, 3);
							break;
					}
					break;
				case 2:
					switch(data[0]){
						case 0x00:
							cmd(1, src_address, COMMAND_VOLUME_PCNT, data[1]);
							break;
						case 0x01:
							cmd(1, src_address, COMMAND_VOLUME_DB, data[1]);
							break;
					}
					break;
			}
			break;
		case CLUNET_COMMAND_MUTE:
			if (size == 1){
				switch(data[0]){
					case 0:
						cmd(1, src_address, COMMAND_VOLUME_PCNT, 0);
						break;
					case 1:
						cmd(1, src_address, COMMAND_MUTE_TOGGLE);
						break;
				}
			}
			break;
		case CLUNET_COMMAND_EQUALIZER:
			switch(size){
				case 1:
					switch(data[0]){
						case 0x00:
							cmd(1, src_address, COMMAND_EQUALIZER_RESET);
							break;
						case 0xFF:
							cmd(1, src_address, COMMAND_EQUALIZER_INFO);
							break;
					}
					break;
				case 2:
				case 3:
					switch(data[0]){
						case 0x01:	//gain
							switch(data[1]){
								case 0x00:	//reset
									cmd(1, src_address, COMMAND_GAIN_DB, 0);
									break;
								case 0x01:	//dB
									if (size == 3){
										cmd(1, src_address, COMMAND_GAIN_DB, data[2]);
									}
									break;
								case 0x02:	//+
									cmd(1, src_address, COMMAND_GAIN_UP);
									break;
								case 0x03:	//-
									cmd(1, src_address, COMMAND_GAIN_DOWN);
									break;
							}
							break;
						case 0x02:	//treble
							switch(data[1]){
								case 0x00:	//reset
									cmd(1, src_address, COMMAND_TREBLE_DB, 0);
									break;
								case 0x01:	//dB
									if (size == 3){
										cmd(1, src_address, COMMAND_TREBLE_DB, data[2]);
									}
									break;
								case 0x02:	//+
									cmd(1, src_address, COMMAND_TREBLE_UP);
									break;
								case 0x03:	//-
									cmd(1, src_address, COMMAND_TREBLE_DOWN);
									break;
							}
							break;
						case 0x03:	//bass
							switch(data[1]){
								case 0x00:	//reset
									cmd(1, src_address, COMMAND_BASS_DB, 0);
									break;
								case 0x01:	//dB
									if (size == 3){
										cmd(1, src_address, COMMAND_BASS_DB, data[2]);
									}
									break;
								case 0x02:	//+
									cmd(1, src_address, COMMAND_BASS_UP);
									break;
								case 0x03:	//-
									cmd(1, src_address, COMMAND_BASS_DOWN);
									break;
							}
							break;
					}
					break;
			}
			break;
	}
}

int main(void){
	cli();
	LED_INIT;
	TWI_INIT;
	
	clunet_init();
	clunet_set_on_data_received(clunet_data_received);
	
	tea5767_init();
	lc75341_init();
	
	sei();
	
	tea5767_set_LO_PLL(99.1);
	lc75341_volume_percent(80);
	
	while (1){}
	return 0;
}

