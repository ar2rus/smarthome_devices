/*
 *  Audio.h
 *
 *  Created: 30.10.2014 21:04:37
 *  Author: gargon
 */ 


#ifndef AUDIO_H_
#define AUDIO_H_

#include "utils/bits.h"

#include <avr/io.h>

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

void cmd(uint8_t sendResponse, uint8_t responseAddress, const uint8_t command, ...);


#endif /* AUDIO_H_ */