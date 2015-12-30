
#include "lc75341/lc75341.h"
#include "nec/nec.h"
#include "tea5767/tea5767.h"

#include "clunet/clunet.h"

#include "Audio.h"


#include <stdarg.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <util/delay.h>

ISR(TIMER1_COMPB_vect){
	if (necReadSignal()){
		OCR1B += NEC_TIMER_CMP_TICKS;
	}else{
		DISABLE_TIMER_CMP_B;
	}
}


char shouldSendDelayedResponse = 0;
unsigned int delayedResponseCounterValue = 0;

//только для предотвращения многократного вызова при удерживании кнопки нажатой
char buttonStates = 0;

ISR(TIMER1_COMPA_vect){
	sei();	//разрешаем прерывания более высокого приоритета (clunet)
	
	if (BUTTON1_READ){
		if (!test_bit(buttonStates, 0)){
			buttonStates = _BV(0);
			cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT, LC75341_INPUT_1);
		}
	}else
	if (BUTTON2_READ){
		if (!test_bit(buttonStates, 1)){
			buttonStates = _BV(1);
			cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT, LC75341_INPUT_2);
		}
	}else
	if (BUTTON3_READ){
		if (!test_bit(buttonStates, 2)){
			buttonStates = _BV(2);
			cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT, LC75341_INPUT_3);
		}
	}else
	if (BUTTON4_READ){
		if (!test_bit(buttonStates, 3)){
			buttonStates = _BV(3);
			cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT, LC75341_INPUT_4);
		}
	}else
	if (BUTTON_L_READ){
		tea5767_search(TEA5767_SUD_DOWN, TEA5767_SSL_LOW);
	}else
	if (BUTTON_R_READ){
		tea5767_search(TEA5767_SUD_UP, TEA5767_SSL_LOW);
	}else{
		buttonStates = 0;
	}

	signed char a = readEncoder();
	if (a > 0){
		shouldSendDelayedResponse = 1;
		delayedResponseCounterValue = TCNT1;
		cmd(0, CLUNET_BROADCAST_ADDRESS, COMMAND_VOLUME_UP, 1);	// не отправляем response сразу
	}else if (a < 0){
		shouldSendDelayedResponse = 1;
		delayedResponseCounterValue = TCNT1;
		cmd(0, CLUNET_BROADCAST_ADDRESS, COMMAND_VOLUME_DOWN, 1);  // не отправляем response сразу
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
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT, LC75341_INPUT_1);
					necResetValue();
					break;
				case 0x40:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT, LC75341_INPUT_2);
					necResetValue();
					break;
				case 0xC0:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT, LC75341_INPUT_3);
					necResetValue();
					break;
				case 0x20:
					cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_INPUT, LC75341_INPUT_4);
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
		cmd(1, CLUNET_BROADCAST_ADDRESS, COMMAND_VOLUME_INFO);
		//also need to save to eeprom
	}
	
	
	OCR1A = TCNT1 + TIMER_NUM_TICKS;
}

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
							cmd(1, src_address, COMMAND_VOLUME_UP, 2);
							break;
						case 0x03:
							cmd(1, src_address, COMMAND_VOLUME_DOWN, 2);
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
	BUTTONS_INIT;
	TWI_INIT;
	
	clunet_init();
	clunet_set_on_data_received(clunet_data_received);
	
	tea5767_init();
	lc75341_init();
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	sei();
	
	tea5767_set_LO_PLL(104.3);
	lc75341_volume_percent(80);
	
	while (1){}
	return 0;
}

