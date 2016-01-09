
#include "KitchenLight.h"

#include <avr/interrupt.h>
#include <util/delay.h>

char buttonStateValue;

void switchExecute(char command){
	switch(command){
		case 0x00:	//откл
			RELAY_0_OFF;
			break;
		case 0x01: //вкл
			RELAY_0_ON;
			break;
		case 0x02: //перекл
			RELAY_0_TOGGLE;
			break;
	}
}

void buttonResponse(unsigned char address){
	char data[] = {BUTTON_ID, buttonStateValue};
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_BUTTON_INFO, data, sizeof(data));
}


void switchResponse(unsigned char address){
	char info = (RELAY_0_STATE << (RELAY_0_ID-1));
	clunet_send_fairy(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_SWITCH:
			if (data[0] == 0xFF){	//info request
				if (size == 1){
					switchResponse(src_address);
				}
			}else{
				if (size == 2){
					switch(data[0]){
						case 0x00:
						case 0x01:
						case 0x02:
						if (data[1] == RELAY_0_ID){
							switchExecute(data[0]);
						}
						break;
						case 0x03:
							switchExecute(bit(data[1], (RELAY_0_ID-1)));
						break;
					}
					switchResponse(CLUNET_BROADCAST_ADDRESS);
				}
			}
			break;
		case CLUNET_COMMAND_BUTTON:
			if (size == 0){
				buttonResponse(src_address);
			}
			break;
	}
}




int main(void){
	
	cli();
	
	RELAY_0_INIT;
	BUTTON_INIT;
	buttonStateValue = BUTTON_READ;
	
	clunet_init();
	clunet_set_on_data_received(clunet_data_received);
	
	sei();
	
	while(1){
		
		char state = BUTTON_READ;
		if (state != buttonStateValue){
			buttonStateValue = state;
			
			if (buttonStateValue){
				//buttonResponse(CLUNET_BROADCAST_ADDRESS);
				switchExecute(0x02);	//toggle
			}
		}
		
		_delay_ms(50`s	= );
		
	}
	
	return 0;
}