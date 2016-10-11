#include "TEA5767.h"

#include <avr/io.h>
#include <util/delay.h>
#include <util/twi.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

//https://github.com/mroger/TEA5767/blob/master/TEA5767N.cpp

static unsigned char write_bytes[5] = { 
	0x00,              //MUTE: 0 - not muted
					   //SEARCH MODE: 0 - not in search mode
					   
	0x00,			   //No frequency defined yet	
	
	0b10110000,	       //SUD: 1 - search up
					   //SSL[1:0]: 01 - low; level ADC output = 5
					   //HLSI: 1 - high side LO injection
					   //MS: 0 - stereo ON
					   //MR: 0 - right audio channel is not muted
					   //ML: 0 - left audio channel is not muted
					   //SWP1: 0 - port 1 is LOW	
					   
	0b00010000,		   //SWP2: 0 - port 2 is LOW
				   	   //STBY: 0 - not in Standby mode
					   //BL: 0 - US/Europe FM band
					   //XTAL: 1 - 32.768 kHz
					   //SMUTE: 0 - soft mute is OFF
					   //HCC: 0 - high cut control is OFF
					   //SNC: 0 - stereo noise canceling is OFF
					   //SI: 0 - pin SWPORT1 is software programmable port 1
					   
	0x00			   //PLLREF: 0 - the 6.5 MHz reference frequency for the PLL is disabled
					   //DTC: 0 - the de-emphasis time constant is 50 ms
};
	
static unsigned char read_bytes[5] =  { 0x00, 0x00, 0x00, 0x00, 0x00 };
	

static uint8_t hiInjection;
static uint8_t muted = 0;

int TEA5767_write(void){
	uint8_t ret = 0;
	
	TWCR = (1<<TWINT)|(1<<TWSTA)|(1<<TWEN);		// I2C start
	while (!(TWCR & (1<<TWINT)));           	// I2C wait
	
	// check value of TWI Status Register. Mask prescaler bits.
	uint8_t twst = TW_STATUS & 0xF8;
	if ((twst != TW_START) && (twst != TW_REP_START)){
		//LOG(("Write error @START, twst = %02X\n", twst));
		ret = 1;
	}		
	
	TWDR = TEA5767_SLA_W;                            	// send address
	TWCR = (1<<TWINT) | (1<<TWEN);
	while (!(TWCR & (1<<TWINT)));
	
	twst = TW_STATUS & 0xF8;
	if ((twst != TW_MT_SLA_ACK) && (twst != TW_MR_SLA_ACK) ){
		//LOG(("Write error @ADDR, twst = %02X\n", twst));
		ret = 1;	
	}	
	
	for (uint8_t i = 0; i < 5; i++){			// send registers
		TWDR = write_bytes[i];
		TWCR = (1<<TWINT) | (1<<TWEN);
		while (!(TWCR & (1<<TWINT)));
		// check value of TWI Status Register. Mask prescaler bits
		twst = TW_STATUS & 0xF8;
		if( twst != TW_MT_DATA_ACK){
			//LOG(("Write error, twst = %02X\n", twst));
			ret = 1;
			break;
		}
	}
	
	TWCR = (1<<TWINT)|(1<<TWEN)|(1<<TWSTO);	// I2C stop
	// wait until stop condition is executed and bus released
	while(TWCR & (1<<TWSTO));	
	return ret;
}

int TEA5767_read(void){
	uint8_t ret = 0;
	TWCR = (1<<TWINT)|(1<<TWSTA)|(1<<TWEN);	// I2C start
	while (!(TWCR & (1<<TWINT)));   
	
	// check value of TWI Status Register. Mask prescaler bits.
	uint8_t twst = TW_STATUS & 0xF8;
	if ((twst != TW_START) && (twst != TW_REP_START)){
		//LOG(("Read error @START, twst = %02X\n", twst));
		ret = 1;
	}	
	
	TWDR = TEA5767_SLA_R;                           	// send address
	TWCR = (1<<TWINT) | (1<<TWEN);
	while (!(TWCR & (1<<TWINT)));
	
	// check value of TWI Status Register. Mask prescaler bits.
	twst = TW_STATUS & 0xF8;
	if ((twst != TW_MT_SLA_ACK) && (twst != TW_MR_SLA_ACK)){
		//LOG(("Read error @ADDR, twst = %02X\n", twst));
		ret = 1;	
	}
		
	for (uint8_t i = 0; i < 5; i++){
		if (i != 4){
			TWACK;
		}
		else{
			TWNACK;
		}
		while (!(TWCR & (1<<TWINT)));
		read_bytes[i] = TWDR;    
	}
	
	TWCR = (1<<TWINT)|(1<<TWEN)|(1<<TWSTO);	// I2C stop
	// wait until stop condition is executed and bus released
	while(TWCR & (1<<TWSTO));	
	return ret;
}

uint8_t TEA5767N_getSignalLevel(uint8_t refresh) {
	//Necessary before read status
	if (refresh){
		TEA5767_write();
	}
	//Read updated status
	TEA5767_read();
	return (read_bytes[3] & TEA5767_ADC_LEVEL) >> 4;
}

void TEA5767N_setSideLOInjection(uint8_t high) {
	if (high){
		write_bytes[2] |= TEA5767_HIGH_LO_INJECT;
		}else{
		write_bytes[2] &= ~TEA5767_HIGH_LO_INJECT;
	}
}

/*example 99.9 -> 9990 */
void TEA5767N_setFrequency(uint16_t _frequency) {
	unsigned int frequencyW;
	
	if (hiInjection) {
		TEA5767N_setSideLOInjection(1);
		frequencyW = 4 * ((_frequency * 10000UL) + 225000UL) / 32768UL;
	} else {
		TEA5767N_setSideLOInjection(0);
		frequencyW = 4 * ((_frequency * 10000UL) - 225000UL) / 32768UL;
	}
	
	write_bytes[0] = ((write_bytes[0] & 0xC0) | ((frequencyW >> 8) & TEA5767_PLL_MASK));
	write_bytes[1] = frequencyW & 0XFF;
}

void TEA5767N_calculateOptimalHiLoInjection(uint16_t frequency) {
	uint8_t signalHigh;
	uint8_t signalLow;
	
	TEA5767N_setSideLOInjection(1);
	TEA5767N_setFrequency(frequency + 45);	//+0.45 MHz
	TEA5767_write();
	
	signalHigh = TEA5767N_getSignalLevel(0);
	
	TEA5767N_setSideLOInjection(0);
	TEA5767N_setFrequency(frequency - 45);	//-0.45 MHz
	TEA5767_write();
	
	signalLow = TEA5767N_getSignalLevel(0);

	hiInjection = (signalHigh < signalLow) ? 1 : 0;
}

void TEA5767N_mono(uint8_t mono) {
	if (mono){
		write_bytes[2] |= TEA5767_MONO;
	}else{
		write_bytes[2] &= ~TEA5767_MONO;
	}
	TEA5767_write();
}

void TEA5767N_mute(uint8_t mute) {
	muted = mute;
	
	if (mute){
		write_bytes[0] |= TEA5767_MUTE;
	}else{
		write_bytes[0] &= ~TEA5767_MUTE;
	}
	TEA5767_write();
}

void TEA5767N_standby(uint8_t on) {
	if (on){
		write_bytes[3] |= TEA5767_STDBY;
	}else{
		write_bytes[3] &= ~TEA5767_STDBY;
	}
	TEA5767_write();
}

void TEA5767N_highCutControl(uint8_t on) {
	if (on){
		write_bytes[3] |= TEA5767_HCC;
	}else{
		write_bytes[3] &= ~TEA5767_HCC;
	}
	TEA5767_write();
}

void TEA5767N_stereoNoiseCancelling(uint8_t on) {
	if (on){
		write_bytes[3] |= TEA5767_SNC;
	}else{
		write_bytes[3] &= ~TEA5767_SNC;
	}
	TEA5767_write();
}

void TEA5767N_selectFrequency(uint16_t frequency) {
	TEA5767N_calculateOptimalHiLoInjection(frequency);
	TEA5767N_setFrequency(frequency);
	TEA5767_write();
}

void TEA5767N_loadFrequency() {
	TEA5767_read();
	
	//Stores the read frequency that can be the result of a search and it's not yet in transmission data
	//and is necessary to subsequent calls to search.
	write_bytes[0] = (write_bytes[0] & 0xC0) | (read_bytes[0] & TEA5767_PLL_MASK);
	write_bytes[1] = read_bytes[1];
}

//99.9 -> 9990
uint16_t TEA5767N_readFrequencyInMHz() {
	TEA5767N_loadFrequency();
		
	unsigned int frequencyW = (((read_bytes[0] & TEA5767_PLL_MASK) << 8) + read_bytes[1]);
	
	if (hiInjection) {
		//return (((frequencyW / 4.0) * 32768.0) - 225000.0) / 1000000.0;
		return  ((frequencyW * 8192) - 225000) / 10000;
	} else {
		//return (((frequencyW / 4.0) * 32768.0) + 225000.0) / 1000000.0;
		return  ((frequencyW * 8192) + 225000) / 10000;
	}
}

uint8_t TEA5767N_isReady() {
	TEA5767_read();
	return (read_bytes[0] & TEA5767_READY_FLAG) != 0;
}

uint8_t TEA5767N_isBandLimitReached() {
	TEA5767_read();
	return (read_bytes[0] & TEA5767_BAND_LIMIT_FLAG) != 0;
}

uint8_t TEA5767N_isStereo() {
	TEA5767_read();
	return (read_bytes[2] & TEA5767_STEREO) != 0;
}

uint8_t TEA5767N_isSearchUp() {
	return (write_bytes[2] & TEA5767_SUD) != 0;
}

uint8_t TEA5767N_isStandBy() {
	TEA5767_read();
	return (write_bytes[3] & TEA5767_STDBY) != 0;
}

uint8_t TEA5767N_isMuted() {
	return muted;
}

uint8_t TEA5767N_searchNext(uint8_t up, uint8_t stop_level) {
	uint8_t bandLimitReached;
	
	if (up) {
		write_bytes[2] |= TEA5767_SUD;
		TEA5767N_selectFrequency(TEA5767N_readFrequencyInMHz() + 10);	//+0.1 MHz
	} else {
		write_bytes[2] &= ~TEA5767_SUD;
		TEA5767N_selectFrequency(TEA5767N_readFrequencyInMHz() - 10);	//-0.1 MHz
	}
	
	//stop levels
	write_bytes[2] &= 0b10011111;
	switch(stop_level){
		case 0:
			write_bytes[2] |= TEA5767_SRCH_LOW_LVL;
			break;
		case 1:
			write_bytes[2] |= TEA5767_SRCH_MID_LVL;
			break;
		case 2:
			write_bytes[2] |= TEA5767_SRCH_HIGH_LVL;
			break;
	}
	
	//Turns the search on
	write_bytes[0] |= TEA5767_SEARCH;
	TEA5767_write();
		
	while(!TEA5767N_isReady()) { }
	//Read Band Limit flag
	bandLimitReached = TEA5767N_isBandLimitReached();
	//Loads the new selected frequency
	TEA5767N_loadFrequency();
	
	//Turns the search off
	write_bytes[0] &= ~TEA5767_SEARCH;
	TEA5767_write();
	
	return bandLimitReached;
}

uint8_t TEA5767N_startSearchFrom(uint16_t frequency, uint8_t up, uint8_t stop_level) {
	TEA5767N_selectFrequency(frequency);
	return TEA5767N_searchNext(up, stop_level);
}

uint8_t TEA5767N_startSearchFromBegin(uint8_t stop_level) {
	return TEA5767N_startSearchFrom(8700, 1, stop_level);				//87.0
}

uint8_t TEA5767N_startSearchFromEnd(uint8_t stop_level) {
	return TEA5767N_startSearchFrom(10800, 0, stop_level);				//108.0
}




char num_channels = -1;	//cache
char cur_channel = -1;

void FM_set_num_channels(uint8_t num){
	num_channels = num;	
	eeprom_write_byte((void*)FM_PROGRAMS_EEPROM_OFFSET, num_channels);
}

void FM_clear_channels(){
	FM_set_num_channels(0);
}

uint8_t FM_get_num_channels(){
	if (num_channels < 0){
		num_channels = eeprom_read_byte((void*)FM_PROGRAMS_EEPROM_OFFSET);
	}
	
	return num_channels;
}

int8_t FM_save_channel(uint8_t num_channel, uint16_t frequency){
	if (num_channel < FM_MAX_NUM_CHANNELS && num_channels <= FM_get_num_channels()){
		eeprom_write_word((void*)FM_PROGRAMS_EEPROM_OFFSET + num_channel*2 + 1, frequency);
		if (FM_get_num_channels() == num_channel){
			FM_set_num_channels(num_channel + 1);
		}
		return num_channel;
	}
	return -1;
}

int8_t FM_add_channel(uint16_t frequency){
	return FM_save_channel(FM_get_num_channels(), frequency);
}

uint16_t FM_get_channel_frequency(uint8_t num_channel){
	if (num_channel < FM_get_num_channels()){
		return eeprom_read_word((void*)FM_PROGRAMS_EEPROM_OFFSET + num_channel*2 + 1);
	}
	return 0;
}

uint8_t FM_select_channel(uint8_t num_channel){
	uint16_t freq = FM_get_channel_frequency(num_channel);
	if (freq){
		cur_channel = num_channel;
		TEA5767N_selectFrequency(freq);
		return 1;
	}
	return 0;
}

uint8_t FM_select_next_channel(uint8_t up){
	if (FM_get_num_channels() > 0){
		if (up){
			cur_channel++;
		}else{
			cur_channel--;
		}
		
		if (cur_channel > FM_get_num_channels()){
			cur_channel = 0;
		} else if (cur_channel < 0){
			cur_channel = FM_get_num_channels() - 1;
		}
		
		return FM_select_channel(cur_channel);
	}
	return 0;
}

void FM_select_frequency(uint16_t frequency){
	TEA5767N_selectFrequency(frequency);
}

void FM_power(uint8_t on){
	TEA5767N_standby(on);
}