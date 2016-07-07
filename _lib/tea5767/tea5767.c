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
	
	
	
static float frequency;
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
 
 
//setup the I2C hardware to ACK the next transmission
//and indicate that we've handled the last one.
#define TWACK (TWCR=(1<<TWINT)|(1<<TWEN)|(1<<TWEA))
//setup the I2C hardware to NACK the next transmission
#define TWNACK (TWCR=(1<<TWINT)|(1<<TWEN)) 
 
int TEA5767_read(void){  // Odczyt danych z TEA5767
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

void TEA5767N_setSideLOInjection(uint8_t high) {
	if (high){
		write_bytes[2] |= TEA5767_HIGH_LO_INJECT;
	}else{
		write_bytes[2] &= ~TEA5767_HIGH_LO_INJECT;
	}
}

void TEA5767N_setFrequency(float _frequency) {
	frequency = _frequency;
	unsigned int frequencyW;
	
	if (hiInjection) {
		TEA5767N_setSideLOInjection(1);
		frequencyW = 4 * ((frequency * 1000000) + 225000) / 32768;
	} else {
		TEA5767N_setSideLOInjection(0);
		frequencyW = 4 * ((frequency * 1000000) - 225000) / 32768;
	}
	
	write_bytes[0] = ((write_bytes[0] & 0xC0) | ((frequencyW >> 8) & 0x3F));
	write_bytes[1] = frequencyW & 0XFF;
}

void TEA5767N_setMute(uint8_t mute) {
	if (mute){
		write_bytes[0] |= TEA5767_MUTE;
	}else{
		write_bytes[0] &= ~TEA5767_MUTE;
	}
}

void TEA5767N_setSearchUp(uint8_t up) {
	if (up){
		write_bytes[2] |= TEA5767_SUD;
	}else{
		write_bytes[2] &= ~TEA5767_SUD;
	}
}

void TEA5767N_setSearchLowStopLevel() {
	write_bytes[2] &= 0b10011111;
	write_bytes[2] |= (1 << 5);
}

void TEA5767N_setSearchMidStopLevel() {
	write_bytes[2] &= 0b10011111;
	write_bytes[2] |= (2 << 5);
}

void TEA5767N_setSearchHighStopLevel() {
	write_bytes[2] &= 0b10011111;
	write_bytes[2] |= (3 << 5);
}

void TEA5767N_setMono(uint8_t mono) {
	if (mono){
		write_bytes[2] |= TEA5767_MONO;
	}else{
		write_bytes[2] &= ~TEA5767_MONO;
	}
}

void TEA5767N_mono(uint8_t mono) {
	TEA5767N_setMono(mono);
	TEA5767_write();
}

void TEA5767N_mute(uint8_t mute) {
	muted = mute;
	TEA5767N_setMute(mute);
	TEA5767_write();
}

uint8_t TEA5767N_isMuted() {
	return muted;
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

void TEA5767N_calculateOptimalHiLoInjection(float freq) {
	uint8_t signalHigh;
	uint8_t signalLow;
	
	TEA5767N_setSideLOInjection(1);
	TEA5767N_setFrequency((float) (freq + 0.45));
	TEA5767_write();
	
	signalHigh = TEA5767N_getSignalLevel(0);
	
	TEA5767N_setSideLOInjection(0);
	TEA5767N_setFrequency((float) (freq - 0.45));
	TEA5767_write();
	
	signalLow = TEA5767N_getSignalLevel(0);

	hiInjection = (signalHigh < signalLow) ? 1 : 0;
}


void TEA5767N_transmitFrequency(float frequency) {
	TEA5767N_setFrequency(frequency);
	TEA5767_write();
}

void TEA5767N_selectFrequency(float frequency) {
	TEA5767N_calculateOptimalHiLoInjection(frequency);
	TEA5767N_transmitFrequency(frequency);
}

void TEA5767N_selectFrequencyMuting(float frequency) {
	TEA5767N_mute(1);
	TEA5767N_selectFrequency(frequency);
	TEA5767N_mute(0);
}

void TEA5767N_loadFrequency() {
	TEA5767_read();
	
	//Stores the read frequency that can be the result of a search and it?s not yet in transmission data
	//and is necessary to subsequent calls to search.
	write_bytes[0] = (write_bytes[0] & 0xC0) | (read_bytes[0] & 0x3F);
	write_bytes[1] = read_bytes[1];
}

float TEA5767N_getFrequencyInMHz(unsigned int frequencyW) {
	if (hiInjection) {
		return (((frequencyW / 4.0) * 32768.0) - 225000.0) / 1000000.0;
	} else {
		return (((frequencyW / 4.0) * 32768.0) + 225000.0) / 1000000.0;
	}
}

float TEA5767N_readFrequencyInMHz() {
	TEA5767N_loadFrequency();
	
	unsigned int frequencyW = (((read_bytes[0] & 0x3F) * 256) + read_bytes[1]);
	return TEA5767N_getFrequencyInMHz(frequencyW);
}

uint8_t TEA5767N_isReady() {
	TEA5767_read();
	return read_bytes[0] >> 7;
}

uint8_t TEA5767N_isBandLimitReached() {
	TEA5767_read();
	return (read_bytes[0] >> 6) & 1;
}

uint8_t TEA5767N_isStereo() {
	TEA5767_read();
	return read_bytes[2] >> 7;
}

uint8_t TEA5767N_isSearchUp() {
	return (write_bytes[2] & 0b10000000) != 0;
}

uint8_t TEA5767N_isStandBy() {
	TEA5767_read();
	return (write_bytes[3] & 0b01000000) != 0;
}

uint8_t TEA5767N_searchNext() {
	uint8_t bandLimitReached;
	
	if (TEA5767N_isSearchUp()) {
		TEA5767N_selectFrequency(TEA5767N_readFrequencyInMHz() + 0.1);
	} else {
		TEA5767N_selectFrequency(TEA5767N_readFrequencyInMHz() - 0.1);
	}
	
	//Turns the search on
	write_bytes[0] |= 0b01000000;
	TEA5767_write();
	
	while(!TEA5767N_isReady()) { }
	//Read Band Limit flag
	bandLimitReached = TEA5767N_isBandLimitReached();
	//Loads the new selected frequency
	TEA5767N_loadFrequency();
	
	//Turns de search off
	write_bytes[0] &= 0b10111111;
	TEA5767_write();
	
	return bandLimitReached;
}

uint8_t TEA5767N_searchNextMuting() {
	uint8_t bandLimitReached;
	
	TEA5767N_mute(1);
	bandLimitReached = TEA5767N_searchNext();
	TEA5767N_mute(0);
	
	return bandLimitReached;
}

uint8_t TEA5767N_startSearchFrom(float frequency) {
	TEA5767N_selectFrequency(frequency);
	return TEA5767N_searchNext();
}

uint8_t TEA5767N_startSearchFromBegin() {
	TEA5767N_setSearchUp(1);
	return TEA5767N_startSearchFrom(87.0);
}

uint8_t TEA5767N_startSearchFromEnd() {
	TEA5767N_setSearchUp(0);
	return TEA5767N_startSearchFrom(108.0);
}

uint8_t TEA5767N_startSearchMutingFromBegin() {
	uint8_t bandLimitReached;
	
	TEA5767N_mute(1);
	bandLimitReached = TEA5767N_startSearchFromBegin();
	TEA5767N_mute(0);
	
	return bandLimitReached;
}

uint8_t TEA5767N_startSearchMutingFromEnd() {
	uint8_t bandLimitReached;
	
	TEA5767N_mute(1);
	bandLimitReached = TEA5767N_startSearchFromEnd();
	TEA5767N_mute(0);
	
	return bandLimitReached;
}