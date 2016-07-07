
#include "tea5767.h"

#include <avr/io.h>

/*unsigned*/ char tea5767WriteBuf[TEA5767_DATA_SIZE] = { 0x00, 0x00, 0x00, 0x10, (TEA5767_BL_US_EURO<<TEA5767_W_BYTE4_BL) | (TEA5767_XTAL_32678KHZ<<TEA5767_W_BYTE4_XTAL)};
/*unsigned*/ char tea5767ReadBuf[TEA5767_DATA_SIZE];


void tea5767_init(){
	TWBR = TWI_TWBR;                                  // Set bit rate register (Baudrate). Defined in header file.
	// TWSR = TWI_TWPS;                               // Not used. Driver presumes prescaler to be 00.
	TWDR = 0xFF;                                      // Default content = SDA released.
	TWCR = (1<<TWEN)|                                 // Enable TWI-interface and release TWI pins.
	(0<<TWIE)|(0<<TWINT)|							  // Disable Interupt.
	(0<<TWEA)|(0<<TWSTA)|(0<<TWSTO)|				  // No Signal requests.
	(0<<TWWC);
}

void tea5767_write(){
	TWCR = (1<<TWINT)|(1<<TWSTA)|(1<<TWEN);			// SEND START SIGNAL
	while (!(TWCR & (1<<TWINT)));           		// WAIT FOR START SIG
	
	TWDR = TEA5767_WRITE_ADDRESS;					// send address
	TWCR = (1<<TWINT) | (1<<TWEN);
	while (!(TWCR & (1<<TWINT)));
	
	for (char i = 0; i < TEA5767_DATA_SIZE; i++ ){
		TWDR = tea5767WriteBuf[i];
		TWCR = (1<<TWINT) | (1<<TWEN);      		// send data
		while (!(TWCR & (1<<TWINT)));
	}
	TWCR = (1<<TWINT)|(1<<TWEN)|(1<<TWSTO); 		//SEND STOP SIGNAL
	while (!(TWCR & (1<<TWSTO)));
}

void tea5767_read(){
	TWCR = (1<<TWINT)|(1<<TWSTA)|(1<<TWEN);			// SEND START SIGNAL
	while (!(TWCR & (1<<TWINT)));           		// WAIT FOR START SIG
	
	TWDR = TEA5767_READ_ADDRESS;					// send address
	TWCR = (1<<TWINT) | (1<<TWEN);
	while (!(TWCR & (1<<TWINT)));
	
	for (char i = 0; i < TEA5767_DATA_SIZE; i++ ){
		TWCR = (1<<TWINT) | (1<<TWEN);      		// read data
		while (!(TWCR & (1<<TWINT)));
		
		tea5767ReadBuf[i] = TWDR;
	}
	TWCR = (1<<TWINT)|(1<<TWEN)|(1<<TWSTO); 		//SEND STOP SIGNAL
	while (!(TWCR & (1<<TWSTO)));
}


void tea5767_set_HI_PLL(double frequency){
	 uint16_t f = 4000*(frequency*1000+225)/32768;
	 
	 tea5767WriteBuf[0] = f >> 8;
	 tea5767WriteBuf[1] = f & 0XFF;
	 
	 tea5767_write();
}

void tea5767_set_LO_PLL(double frequency){
	uint16_t f = 4000*(frequency*1000-225)/32768;
	
	tea5767WriteBuf[0] = f >> 8;
	tea5767WriteBuf[1] = f & 0XFF;
	
	tea5767_write();
}

void tea5767_unmute(){
	unset_bit(tea5767WriteBuf[0], TEA5767_W_BYTE1_MUTE);
		tea5767_write();
}

char tea5767_search(char up, char ssl){
	tea5767WriteBuf[2] = (tea5767WriteBuf[2] & 0x1F) | (up<<TEA5767_W_BYTE3_SUD) | (ssl << TEA5767_W_BYTE3_SSL);
	set_bit2(tea5767WriteBuf[0], TEA5767_W_BYTE1_MUTE, TEA5767_W_BYTE1_SEARCH_MODE);
	tea5767_write();
	
	while(1){
		tea5767_read();
		if (test_bit(tea5767ReadBuf[0], TEA5767_R_BYTE1_READY)){
			break;
		}
	}
	
	//tea5767WriteBuf[0] = tea5767ReadBuf[0] & 0x3F;
	//tea5767WriteBuf[1] = tea5767ReadBuf[1];
	
	//tea5767_write();
	tea5767_unmute();
	return 1;
}