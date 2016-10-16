/*
 *  Audio.h
 *
 *  Created: 30.10.2014 21:04:37
 *  Author: gargon
 */ 


#ifndef AUDIO_H_
#define AUDIO_H_

#include "utils/bits.h"
#include "clunet/clunet.h"
#include "clunet/clunet_buffered.h"

#include "lc75341/lc75341.h"
#include "tea5767/tea5767.h"


#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

/*led port description*/
#define LED_PORT B
#define LED_PIN  6

/*led controls*/
#define LED_INIT set_bit(DDRPORT(LED_PORT), LED_PIN)
#define LED_ON   set_bit(OUTPORT(LED_PORT), LED_PIN)
#define LED_OFF  unset_bit(OUTPORT(LED_PORT), LED_PIN)


/* main timer controls*/
#define TIMER_PRESCALER 8
#define TIMER_DELAY 1e-3	/*1ms main loop*/
#define TIMER_NUM_TICKS (unsigned int)(TIMER_DELAY * F_CPU / TIMER_PRESCALER)
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit(TCCR1B, CS11); unset_bit2(TCCR1B, CS10, CS12); /*8x prescaler*/}

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define TIMER_COMP_VECTOR TIMER1_COMPA_vect


/* skip events delay */
#define TIMER_SKIP_EVENTS_DELAY (unsigned int)(250e-3/TIMER_DELAY)		/* 500ms */

/* i2c controls */
#define TWI_INIT {set_bit(OUTPORT(C), 4); set_bit(OUTPORT(C), 5);}

#define EEPROM_CONFIG_ADDRESS 0x00

typedef struct{
	int8_t power;
	uint8_t input;
	int8_t volume;
	
	uint8_t eq_gain;
	int8_t eq_treble;
	uint8_t eq_bass;
	
	int8_t fm_channel;
	uint16_t fm_freq;
	uint8_t fm_controls;
} config;

#endif /* AUDIO_H_ */