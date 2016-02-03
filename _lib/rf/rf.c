
#include <util/delay.h>
#include <string.h>

#include "rf.h"


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

void rf_send_package(char* data, unsigned char count){
	RF_TX_HI;
	_delay_us(RF_START_BIT1_DELAY);
	RF_TX_LO;
	_delay_us(RF_START_BIT0_DELAY);
	RF_TX_HI;
	_delay_us(RF_START_BIT1_DELAY);
	/*RF_TX_LO;
	_delay_us(RF_START_BIT0_DELAY);
	RF_TX_HI;
	_delay_us(RF_START_BIT1_DELAY);
	*/
	
	
	for (unsigned char i=0; i<count; i++){
		char c = data[i];
		for (unsigned char b=0; b<8; b++){
				RF_TX_LO;
				_delay_us(RF_BIT0_DELAY);
				if (test_bit(c, b)){
					_delay_us(RF_BIT1_DELAY - RF_BIT0_DELAY);
				}
				RF_TX_HI;
				_delay_us(RF_HI_DELAY);
		}
	}
	RF_TX_LO;
}

volatile unsigned char tx_message_id = 0;

void rf_send_message(unsigned char device_id, char* data, unsigned char num_repeats){
	
	unsigned char data_len = RF_DEVICES_DATA_LENGTH[device_id];
	unsigned char msg_len =  data_len + 3;
	
	char message[msg_len];
	message[0] = RF_MESSAGE_PREAMBULE_HEADER | (device_id & 0b11);
	message[1] = tx_message_id++;
	memcpy(&message[2], data, data_len);
	message[msg_len - 1] = check_crc(&message[0], msg_len - 1);
	
	for (unsigned char i=0; i<num_repeats; i++){
		//message[2] = (i==0 ? 0x30 : (i==1 ? 0x18 : 0x7a));
		//message[msg_len - 1] = check_crc(&message[0], msg_len - 1);
		rf_send_package(&message[0], msg_len);
		_delay_us(100);
	}
}


//debugging
#define LED_PORT B
#define LED_PIN  4

#define LED_INIT {set_bit(DDRPORT(LED_PORT), LED_PIN); LED_OFF;}
#define LED_ON set_bit(OUTPORT(LED_PORT), LED_PIN)
#define LED_OFF unset_bit(OUTPORT(LED_PORT), LED_PIN)


#ifdef RF_RX_INIT
//=============================================================================
//   RECEIVE_RF_PACKET
//=============================================================================


void rf_receive_packet(char* data, unsigned char num_to_recieve, unsigned char preambule){
	
	RF_RX_INIT;
	
	unsigned char rrp_data;
	unsigned char rrp_period;
	unsigned char rrp_bits;
	unsigned char rrp_bytes = 0;
	
	
	while(rrp_bytes < num_to_recieve){
		
		rrp_bytes = 0;
		//try to recieve start pulses
		while(1){
			/*RF_TIMER_REG = 0;
			while(RF_VAL);
			rrp_period = RF_TIMER_REG;
			RF_TIMER_REG = 0;
			if(rrp_period < RF_NUM_TICKS_START){
				continue;
			}
		
			while(!RF_VAL);*/
			
			RF_TIMER_REG = 0;
			while(RF_VAL);
			rrp_period = RF_TIMER_REG;
			RF_TIMER_REG = 0;
			if(rrp_period < RF_NUM_TICKS_START){
				continue;
			}
			
			while(!RF_VAL);
			
			RF_TIMER_REG = 0;
			while(RF_VAL);
			rrp_period = RF_TIMER_REG;
			RF_TIMER_REG = 0;
			if(rrp_period < RF_NUM_TICKS_START){
				continue;
			} else {
				break;
			}
		}
		//LED_ON;
	
	
		while(rrp_bytes < num_to_recieve){
	
			rrp_bits = 8;

			rrp_data = 0;
			while(rrp_bits){
				while(!RF_VAL);
				while(RF_VAL);
				rrp_period = RF_TIMER_REG;		// grab the pulse period!
				RF_TIMER_REG = 0;				// and ready to record next period

				if(rrp_period >= RF_NUM_TICKS_START){			// if >=2000uS, is unexpected start pulse!
					break;
				}

				rrp_data >>= 1;
				if(rrp_period > RF_NUM_TICKS_BIT){				//  125uS
					set_bit(rrp_data, 7);
				}
				rrp_bits--;
			}
		
		
			if(rrp_bits){        // if error
					break;
			} else{							// else 8 good bits were received
				
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
//-----------------------------------------------------------------------------


volatile unsigned char rx_message_id = 255;

char rf_recieve_message(unsigned char device_id, char* data){
	
	unsigned char preambule = RF_MESSAGE_PREAMBULE_HEADER | (device_id & 0b11);
	unsigned char data_len = RF_DEVICES_DATA_LENGTH[device_id];
	unsigned char msg_len = data_len + 3;
	
	char message[msg_len];
	
	rf_receive_packet(&message[0], msg_len, preambule);
	
	if (check_crc(&message[0], msg_len - 1) == message[msg_len - 1]){
		if (rx_message_id != message[1]){	//отфильтровываем сообщения дубликаты с тем же номером
			rx_message_id = message[1];
			memcpy(data, &message[2], data_len);
			return 1;
		}
	}
	return 0;
}

#endif