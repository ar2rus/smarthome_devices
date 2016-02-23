
#include "rx.h"
#include <util/delay.h>
#include <avr/interrupt.h>
#include <string.h>

volatile unsigned char counter = 0;

//PWM 0 -> 128 -> 0
 ISR(TIM0_COMPA_vect){
 	counter++;
 }

ISR(TIM0_OVF_vect){
	counter++;
}

void nec_delay(unsigned int ticks){
	
	unsigned char w = (ticks>>8);
	unsigned char r = (ticks & 0xFF);
	
	for (unsigned char i=0; i<=w; i++){
		unsigned char lim;
		if (i<w){
			lim = 0xFF;
		}else{
			lim = r;
		}
		
		unsigned char cv = counter;
		while ((unsigned char)(counter - cv) < lim);
	}
}

void nec_send(char address, char command){
	
	unsigned long data = address;
	data <<= 8;
	data |= (char)(~address);
	data <<= 8;
	data |= command;
	data <<= 8;
	data |= (char)(~command);
	
	TIMER_ENABLE_PWM;	
	//nec_delay(NEC_HDR_MARK_T);
	_delay_us(NEC_HDR_MARK);

	TIMER_DISABLE_PWM;
	//nec_delay(NEC_HDR_SPACE_T);
	_delay_us(NEC_HDR_SPACE);
	
	for (int i = 0; i < 32; i++) {
		
		TIMER_ENABLE_PWM;
		//nec_delay(NEC_BIT_MARK_T);
		_delay_us(NEC_BIT_MARK);
			
		TIMER_DISABLE_PWM;
		if (data & NEC_TOPBIT) {	
			//nec_delay(NEC_ONE_SPACE_T);
			_delay_us(NEC_ONE_SPACE);
		} else {
			//nec_delay(NEC_ZERO_SPACE_T);
			_delay_us(NEC_ZERO_SPACE);
		}
		data <<= 1;
	}

	
	TIMER_ENABLE_PWM;
	//nec_delay(NEC_BIT_MARK_T);
	_delay_us(NEC_BIT_MARK);
	TIMER_DISABLE_PWM;
}


//таблица соответствия ID устройства и длины полезных данных в его сообщении
const unsigned char RF_DEVICES_DATA_LENGTH[4] = {0, 0, RF_RGB_LIGHTS_DATA_LEN, 0};


char check_crc(char* data, unsigned char size){
	uint8_t crc=0;
	uint8_t i,j;
	for (i=0; i<size;i++){
		uint8_t inbyte = data[i];
		for (j=0;j<8;j++){
			uint8_t mix = (crc ^ inbyte) & 0x01;
			crc >>= 1;
			if (mix){
				crc ^= 0x8C;
			}
			inbyte >>= 1;
		}
	}
	return crc;
}

void rf_receive_packet(char* data, unsigned char num_to_recieve, unsigned char preambule){
	unsigned char rrp_data;
	unsigned char rrp_period;
	unsigned char rrp_bits;
	unsigned char rrp_bytes = 0;

	while(rrp_bytes < num_to_recieve){

		unsigned char cv;

		rrp_bytes = 0;
		while(1){
			cv = counter;
			while(RF_VAL);
			rrp_period = counter - cv;
			cv = counter;
			if(rrp_period < RF_NUM_TICKS_START){
				continue;
			} else {
				break;
			}
		}
		
		while(rrp_bytes < num_to_recieve){

			rrp_bits = 8;
			rrp_data = 0;
			while(rrp_bits){
				while(!RF_VAL);
				while(RF_VAL);
				rrp_period = counter - cv;
				cv = counter;
				if(rrp_period >= RF_NUM_TICKS_START){
					break;
				}

				rrp_data >>= 1;
				if(rrp_period > RF_NUM_TICKS_BIT){
					set_bit(rrp_data, 7);
				}
				rrp_bits--;
			}

			if(rrp_bits){
				break;
			} else{
				//здесь проверка на совпадении первого байта(преамбулы) сообщения (пакета)
				//преамбула не сравнивается, если в нее передан 0
				if (preambule != 0 && rrp_bytes == 0 && rrp_data != preambule){
					break;
				}
				data[rrp_bytes++] = rrp_data;		// so save the received byte into array
			}
		}
	}
}

volatile unsigned char rx_message_id = 0xff;

char rf_recieve_message(unsigned char device_id, char* data){
	
	unsigned char preambule = RF_MESSAGE_PREAMBULE_HEADER | (device_id & 0b11);
	unsigned char data_len = RF_DEVICES_DATA_LENGTH[device_id];
	unsigned char msg_len = data_len + 3;
	
	char message[msg_len];
	
	rf_receive_packet(&message[0], msg_len, preambule);
	
	if (check_crc(&message[0], msg_len - 1) == message[msg_len - 1]){
		if (rx_message_id != message[1]){
			rx_message_id = message[1];
			memcpy(data, &message[2], data_len);
			return 1;
		}
	}
	return 0;
}


char data[RF_RGB_LIGHTS_DATA_LEN];

int main(void){
	cli();
	
// 	LED_INIT;
// 	LED_ON;
// 	_delay_ms(200);
// 	LED_OFF;
// 	
	RF_RX_INIT;
	NEC_INIT;
    
	sei();
	
	while(1){
		
		
 		if (rf_recieve_message(RF_RGB_LIGHTS_ID, &data[0])){
 			cli();
 			nec_send(0x00, data[RGB_LIGHT_ID]);
 			sei();
 		}

	}
	return 0;
}