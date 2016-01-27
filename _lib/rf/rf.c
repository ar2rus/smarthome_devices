

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


//=============================================================================
//   SEND_RF_BYTE
//=============================================================================
void rf_send_byte(char data){
	//-------------------------------------------------------
	// This is a pulse period encoded system to send a byte to RF module.
	// Bits are sent MSB first. Each byte sends 9 pulses (makes 8 periods).
	// Timing;
	//   HI pulse width; always 80uS
	//   0 bit, LO width; 20uS (total 0 bit pulse period 100uS)
	//   1 bit, LO width; 70uS (total 1 bit pulse period 150uS)
	//   space between bytes, LO width; 170uS (total space period 250uS)
	//-------------------------------------------------------
	
	// make 250uS start bit first
	RF_TX_LO;
	_delay_us(RF_START_BIT_DELAY - RF_HI_DELAY);		  // 170uS LO
	RF_TX_HI;
	_delay_us(RF_HI_DELAY);							      // 80uS HI
	RF_TX_LO;

	// now loop and send 8 bits
	for(char tbit=0; tbit<8; tbit++){
		_delay_us(RF_BIT0_DELAY - RF_HI_DELAY);			 // default 0 bit LO period is 20uS
		if(test_bit(data, 7 - tbit)){
			_delay_us(RF_BIT1_DELAY - RF_BIT0_DELAY);	 // increase the LO period if is a 1 bit!
		}
		RF_TX_HI;
		_delay_us(RF_HI_DELAY);							// 80uS HI pulse
		RF_TX_LO;
	}
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
		for (unsigned char j=0; j<msg_len; j++){
			rf_send_byte(message[j]);
		}
	}
}



#ifdef RF_RX_INIT
//=============================================================================
//   RECEIVE_RF_PACKET
//=============================================================================
void rf_receive_packet(char* data, unsigned char num_to_recieve, char preambule){
	//-------------------------------------------------------
	// This function receives an RF packet of bytes in my pulse period
	// encoded format. The packet must have 10 valid contiguous bytes
	// or the function will not exit. There is no timeout feature, but could be added.
	// global variable; unsigned char rxdat[10] holds the 10 byte result.
	//-------------------------------------------------------
	unsigned char rrp_data;
	unsigned char rrp_period;
	unsigned char rrp_bits;
	unsigned char rrp_bytes;

	rrp_bytes = 0;
	while(rrp_bytes < num_to_recieve){   // loop until it has received num_to_recieve contiguous RF bytes
		
		RF_TIMER_REG = 0;
		//-----------------------------------------
		// wait for a start pulse >200uS
		while(1){
			while(!RF_VAL) continue;    // wait for input / edge
			while(RF_VAL) continue;     // wait for input \ edge
			rrp_period = RF_TIMER_REG;  // grab the pulse period!
			RF_TIMER_REG = 0;           // and ready to record next period
			if(rrp_period < RF_NUM_TICKS_START){		  // clear bytecount if still receiving noise
				rrp_bytes = num_to_recieve;	//just to skip next cycle
			} else {
				break;                   // exit if pulse was >200uS
			}
		}
		
		//-----------------------------------------
		// now we had a start pulse, record 8 bits
		rrp_bits = 8;
		if (rrp_bytes < num_to_recieve){
			rrp_data = 0;
			while(rrp_bits){
				while(!RF_VAL) continue;		// wait for input / edge
				while(RF_VAL) continue;			// wait for input \ edge
				rrp_period = RF_TIMER_REG;		// grab the pulse period!
				RF_TIMER_REG = 0;				// and ready to record next period

				if(rrp_period >= RF_NUM_TICKS_START){			// if >=200uS, is unexpected start pulse!
					break;
				}

				rrp_data <<= 1;
				if(rrp_period > RF_NUM_TICKS_BIT){				//  125uS
					rrp_data |= 1;
				}
				rrp_bits--;                   // and record 1 more good bit done
			}
		}
		
		//-----------------------------------------
		// gets to here after 8 good bits OR after an error (unexpected start pulse)
		if(rrp_bits){        // if error
				rrp_bytes = 0;   // reset bytes, must run from start of a new packet again!
			} else{							// else 8 good bits were received
				
				//здесь проверка на совпадении первого байта(преамбулы) сообщения (пакета)
				//преамбула не сравнивается, если в нее передан 0
				if (preambule != 0 && rrp_bytes == 0 && rrp_data != preambule){
					continue;
				}
				
				data[rrp_bytes] = rrp_data;		// so save the received byte into array
				rrp_bytes++;                    // record another good byte was saved
		}
	}
}
//-----------------------------------------------------------------------------


volatile unsigned char rx_message_id = 255;

char rf_recieve_message(unsigned char device_id, char* data){
	
	char preambule = RF_MESSAGE_PREAMBULE_HEADER | (device_id & 0b11);
	unsigned char data_len = RF_DEVICES_DATA_LENGTH[device_id];
	unsigned char msg_len = data_len + 3;
	
	char message[msg_len];
	
	rf_receive_packet(&message[0], msg_len, preambule);
	
	if (check_crc(data, msg_len - 1) == message[msg_len - 1]){
		if (rx_message_id != message[1]){	//отфильтровываем сообщения дубликаты с тем же номером
			rx_message_id = message[1];
			memcpy(data, &message[2], data_len);
			return 1;
		}
	}
	return 0;
}

#endif