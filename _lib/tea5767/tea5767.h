/** \file
	\brief TEA5767 FM radio
	
	TEA5767 FM tuner. Tested with module from Philips SA3115/02 mp3 player in I2C mode.
	This module is using XTAL 32768.
	Module markings:
		FXO 55D(H)
		M230-55D(H)
		DS206
	
	Prerequisitions: configured I2C (TWI) hardware
*/
#ifndef TEA5767_H
#define TEA5767_H

#include <stdint.h>

int TEA5767_init(void);
void TEA5767_handle(void);

/** \param value Tuned frequency in kHz */
void TEA5767_tune(uint32_t value);

void TEA5767_search(uint8_t up);
void TEA5767_exit_search(void);
int TEA5767_write(void);


struct TEA5767_status
{
	uint32_t frequency;
	uint8_t ready;
	uint8_t band_limit;
	uint8_t tuned;
	uint8_t stereo;
	uint8_t rx_power;
};

int TEA5767_get_status(struct TEA5767_status *status);

void TEA5767_search_up();
void TEA5767_search_down();

#endif
