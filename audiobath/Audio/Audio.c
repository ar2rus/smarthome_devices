
#include "lc75341/lc75341.h"
#include "tea5767/tea5767.h"

#include "clunet/clunet.h"

#include "Audio.h"


#include <stdarg.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <util/delay.h>

volatile unsigned int systime = 0;

volatile char shouldSendDelayedResponse = 0;
volatile unsigned int delayedResponseCounterValue = 0;

volatile unsigned char rcv = 0;
volatile unsigned char rcv_src_address;
volatile unsigned char rcv_dst_address;
volatile unsigned char rcv_command;
char rcv_data[5];
volatile unsigned char rcv_size;

void cmd(){
	LED_ON;
	char response = -1;
	
	switch(rcv_command){
		
		case CLUNET_COMMAND_CHANNEL:
		switch (rcv_size){
			case 1:
			switch (rcv_data[0]){
				case 0xFF:
					response = 0;
					break;
				case 0x01:
					lc75341_input_next();
					response = 0;
					break;
				case 0x02:
					lc75341_input_prev();
					response = 0;
					break;
			}
			break;
			case 2:
			switch(rcv_data[0]){
				case 0x00:
					lc75341_input(rcv_data[1] - 1);	//1 channel -> 0 channel, etc
					response = 0;
					break;
			}
			break;
		}
		break;
		
		case CLUNET_COMMAND_VOLUME:
		switch (rcv_size){
			case 1:
			switch (rcv_data[0]){
				case 0xFF:
					response = 1;
					break;
				case 0x02:
					shouldSendDelayedResponse = 1;
					delayedResponseCounterValue = systime;
					lc75341_volume_up(2);
					response = 1;
					break;
				case 0x03:
					shouldSendDelayedResponse = 1;
					delayedResponseCounterValue = systime;
					lc75341_volume_down(2);
					response = 1;
					break;
			}
			break;
			case 2:
			switch(rcv_data[0]){
				case 0x00:
					lc75341_volume_percent(rcv_data[1]);
					break;
				case 0x01:
					lc75341_volume_dB(rcv_data[1]);
					break;
			}
			break;
		}
		break;
		
		case CLUNET_COMMAND_MUTE:
		if (rcv_size == 1){
			switch(rcv_data[0]){
				case 0:
					lc75341_volume_percent(0);
					break;
				case 1:
					lc75341_mute_toggle();
					response = 1;
					break;
			}
		}
		break;
		
		case CLUNET_COMMAND_EQUALIZER:
		switch(rcv_size){
			case 1:
			switch(rcv_data[0]){
				case 0x00:
					lc75341_reset_equalizer();
					response = 2;
					break;
				case 0xFF:
					response = 2;
					break;
			}
			break;
			case 2:
			case 3:
			switch(rcv_data[0]){
				case 0x01:	//gain
				switch(rcv_data[1]){
					case 0x00:	//reset
						lc75341_gain_dB(0);
						response = 2;
						break;
					case 0x01:	//dB
						if (rcv_size == 3){
							lc75341_gain_dB(rcv_data[2]);
							response = 2;
						}
						break;
					case 0x02:	//+
						lc75341_gain_up();
						response = 2;
						break;
					case 0x03:	//-
						lc75341_gain_down();
						response = 2;
						break;
				}
				break;
				case 0x02:	//treble
				switch(rcv_data[1]){
						case 0x00:	//reset
						lc75341_treble_dB(0);
						response = 2;
						break;
					case 0x01:	//dB
						if (rcv_size == 3){
							lc75341_treble_dB(rcv_data[2]);
							response = 2;
						}
						break;
					case 0x02:	//+
						lc75341_treble_up();
						response = 2;
						break;
					case 0x03:	//-
						lc75341_treble_down();
						response = 2;
						break;
				}
				break;
				case 0x03:	//bass
				switch(rcv_data[1]){
					case 0x00:	//reset
						lc75341_bass_dB(0);
						response = 2;
						break;
					case 0x01:	//dB
						if (rcv_size == 3){
							lc75341_bass_dB(rcv_data[2]);
							response = 2;
						}
						break;
					case 0x02:	//+
						lc75341_bass_up();
						response = 2;
						break;
					case 0x03:	//-
						lc75341_bass_down();
						response = 2;
						break;
				}
				break;
			}
			break;
		}
		break;
	}
	
	if (response){
		switch (response){
			case 0:{
				char channel = lc75341_input_value() + 1;	//0 channel -> to 1 channel
				clunet_send(rcv_src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_CHANNEL_INFO, &channel, sizeof(channel));
				break;
			}
			case 1:{
				char data[2];
				data[0] = lc75341_volume_percent_value();
				data[1] = lc75341_volume_dB_value();
				clunet_send(rcv_src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_VOLUME_INFO, (char*)&data, sizeof(data));
				break;
			}
			case 2:{
				char data[3];
				data[0] = lc75341_gain_dB_value();
				data[1] = lc75341_treble_dB_value();
				data[2] = lc75341_bass_dB_value();
				clunet_send(rcv_src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_EQUALIZER_INFO, (char*)&data, sizeof(data));
				break;
			}
		}
	}
	
	LED_OFF;
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	if (!rcv){
		rcv = 1;
		rcv_src_address = src_address;
		rcv_dst_address = dst_address;
		rcv_command = command;
		rcv_size = size;
		memcpy(&rcv_data[0], data, size);
	}
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
	clunet_init();
	
	while (1){
		++systime;
		
		if (rcv){
			cmd();
			rcv = 0;
		}
		
		//send delayed response
		//отправляем не раньше чем через TIMER_SKIP_EVENTS_DELAY мс, 
		//но все равно рвется прерыванием и отправляется только по окончании любой длительной операции
		if (shouldSendDelayedResponse && (systime - delayedResponseCounterValue >= TIMER_SKIP_EVENTS_DELAY)){
			shouldSendDelayedResponse = 0;
			//cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_VOLUME_INFO);
			//also need to save to eeprom
		}
		_delay_ms(1);
	}
	return 0;
	
}

