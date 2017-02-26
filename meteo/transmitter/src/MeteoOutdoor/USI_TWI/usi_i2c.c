
#include <avr/io.h>
#include <util/delay.h>
#include <compat/deprecated.h>
#include "usi_i2c.h"

#define USISR_8BIT 0xF0
#define USISR_1BIT 0xFE


void init_usi_i2c_master(void);
void i2c_start(void);
void i2c_stop(void);
uint8_t usi_i2c_master_transfer(uint8_t tmp_sr);
uint8_t i2c_master_send(uint8_t *data, uint8_t data_size);
uint8_t i2c_master_read(uint8_t *data, uint8_t data_size);

void init_usi_i2c_master(void)
{

	// init USI
	sbi(sclportd,scl);
	sbi(sdaportd,sda);                      // Preload dataregister with "released level" data.
	USICR    =  (0<<USISIE)|(0<<USIOIE)|                            // Disable Interrupts.
	(1<<USIWM1)|(0<<USIWM0)|                            // Set USI in Two-wire mode.
	(1<<USICS1)|(0<<USICS0)|(1<<USICLK)|                // Software stobe as counter clock source
	(0<<USITC);
	USISR   =   (1<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|      // Clear flags,
	(0x0<<USICNT0);
	sbi(sdaport,sda);
	sbi(sclport,scl);
	return;
}

void i2c_start(void)
{
	
	sbi(sdaport,sda);
	sbi(sclport,scl);
	cbi(sclportd,scl);
	USISR = (1<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|(0x0<<USICNT0);
	cbi(sdaport,sda);
	_delay_us(USI_DELAY);
	sbi(sclportd,scl);
	cbi(sclport,scl);
	sbi(sdaport,sda);
	_delay_us(USI_DELAY);
	return;
}

void i2c_stop(void)
{

	cbi(sclport,scl);
	_delay_us(USI_DELAY);
	cbi(sdaport,sda);
	_delay_us(USI_DELAY);
	sbi(sclport,scl);
	_delay_us(USI_DELAY);
	sbi(sdaport,sda);
	_delay_us(USI_DELAY);
	USISR|=(1<<USIPF);
	return;
}

uint8_t usi_i2c_master_transfer(uint8_t tmp_sr)
{

	USISR = tmp_sr;
	tmp_sr= (0<<USISIE)|(0<<USIOIE)|
	(1<<USIWM1)|(0<<USIWM0)|
	(1<<USICS1)|(0<<USICS0)|(1<<USICLK)|
	(1<<USITC);
	do {
		_delay_us(USI_DELAY);
		USICR=tmp_sr;
		while (bit_is_clear(sclportinp,scl));
		_delay_us(USI_DELAY);
		USICR=tmp_sr;
	} while (!(USISR&(1<<USIOIF)));
	_delay_us(USI_DELAY);
	tmp_sr=USIDR;
	USIDR=0xff;
	sbi(sdaportd,sda);
	return (tmp_sr);

}

uint8_t i2c_master_send(uint8_t *data, uint8_t data_size)
{

	//if (bit_is_set(sclportinp,scl)) return(1);

	do {
		USIDR=(*(data++));
		usi_i2c_master_transfer(USISR_8BIT);
		cbi(sdaportd,sda);
		if ((usi_i2c_master_transfer(USISR_1BIT))&0x01) return(2);
		data_size--;
	} while (data_size>0);
	return(0);
}

uint8_t i2c_master_read(uint8_t *data, uint8_t data_size)
{

	//if (bit_is_set(sclportinp,scl)) return(1);
	do {
		cbi(sdaportd,sda);
		*(data++)=usi_i2c_master_transfer(USISR_8BIT);
		if (data_size==1) {
			USIDR=0xFF;
		}
		else {
			USIDR=0x00;
		}
		usi_i2c_master_transfer(USISR_1BIT);
		data_size--;
	} while (data_size>0);
	return(0);

}

//--------  end of file   -------------------------
