/*
 *  Audio.h
 *
 *  Created: 30.10.2014 21:04:37
 *  Author: gargon
 */ 


#ifndef AUDIO_H_
#define AUDIO_H_

#include "utils/bits.h"

#include "lc75341/lc75341.h"
#include "tea5767/tea5767.h"

#include "clunet/clunet.h"
#include "clunet/clunet_buffered.h"

#include <string.h>
#include <stdarg.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

#include <util/delay.h>


/* main timer controls*/
#define TIMER_PRESCALER 64
#define TIMER_NUM_TICKS (unsigned int)(1e-3 * F_CPU / TIMER_PRESCALER)	/*1ms main loop*/
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit2(TCCR1B, CS11, CS10); unset_bit(TCCR1B, CS12); /*64x prescaler*/}

#define TIMER_REG TCNT1

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define TIMER_COMP_VECTOR TIMER1_COMPA_vect


/* skip events delay */
#define TIMER_SKIP_EVENTS_DELAY 500		/* 500ms */


/*led port description*/
#define LED_PORT B
#define LED_PIN  6

/*led controls*/
#define LED_INIT set_bit(DDRPORT(LED_PORT), LED_PIN)
#define LED_ON   set_bit(OUTPORT(LED_PORT), LED_PIN)
#define LED_OFF  unset_bit(OUTPORT(LED_PORT), LED_PIN)

/* i2c controls */
#define TWI_INIT {set_bit(OUTPORT(C), 4); set_bit(OUTPORT(C), 5);}

/*commands descriptions*/
#define COMMAND_INPUT			1
#define COMMAND_INPUT_NEXT		2
#define COMMAND_INPUT_PREV		3
#define COMMAND_INPUT_INFO		4

#define COMMAND_MUTE_TOGGLE		10
#define COMMAND_VOLUME_DB		11
#define COMMAND_VOLUME_PCNT		12
#define COMMAND_VOLUME_UP		13
#define COMMAND_VOLUME_DOWN		14
#define COMMAND_VOLUME_INFO		15

#define COMMAND_TREBLE_DB		20
#define COMMAND_TREBLE_UP		21
#define COMMAND_TREBLE_DOWN		22

#define COMMAND_BASS_DB			30
#define COMMAND_BASS_UP			31
#define COMMAND_BASS_DOWN		32

#define COMMAND_GAIN_DB			40
#define COMMAND_GAIN_UP			41
#define COMMAND_GAIN_DOWN		42

#define COMMAND_EQUALIZER_RESET	50
#define COMMAND_EQUALIZER_INFO	51



#define RESPONSE_CHANNEL   0
#define RESPONSE_VOLUME    1
#define RESPONSE_EQUALIZER 2
#define RESPONSE_POWER 3


//тек.состояние (вкл/выкл)
#define EEPROM_ADDRESS_AUDIOBATH_POWER 0x00


#endif /* AUDIO_H_ */