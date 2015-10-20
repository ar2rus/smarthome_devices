/*
 * lc75341_ccb.h
 *
 * Created: 22.10.2014 20:16:25
 *  Author: gargon
 */ 


#ifndef LC75341_H_
#define LC75341_H_

#include "lc75341_config.h"
#include "utils/bits.h"


// Base delay (us).  Also used to time the CL (clock) line.
// 100us should be enough even for slow CCB devices.
#define CCB_DELAY 100

/**CCB address for LC75341**/
#define LC75341_ADDRESS 0x82

#define LC75341_INPUT_1 0
#define LC75341_INPUT_2 1
#define LC75341_INPUT_3 2
#define LC75341_INPUT_4 3

#define LC75341_GAIN_MIN  0	//0 dB
#define LC75341_GAIN_MAX 15	//+30 dB

#define LC75341_VOLUME_MAX 	0 // 0dB
#define LC75341_VOLUME_MIN 80 // -80 dB

#define LC75341_TREBLE_MIN -5	//-10 dB
#define LC75341_TREBLE_MAX  5	//+10 dB

#define LC75341_BASS_MIN 0		//0 dB
#define LC75341_BASS_MAX 10		//+20 dB


void lc75341_init();
void lc75341_reset();

char lc75341_input(unsigned char input);
char lc75341_input_next();
char lc75341_input_prev();
unsigned char lc75341_input_value();


char lc75341_volume_dB(signed char volume);
char lc75341_volume_percent(unsigned char volume);
char lc75341_volume_up(char step);
char lc75341_volume_down(char step);

char lc75341_volume_up_exp(unsigned char step);
char lc75341_volume_down_exp(unsigned char step);

char lc75341_mute();
char lc75341_unmute();
void lc75341_mute_toggle();

signed char lc75341_volume_dB_value();
unsigned char lc75341_volume_percent_value();


char lc75341_gain_dB(unsigned char gain);
char lc75341_gain_up();
char lc75341_gain_down();
unsigned char lc75341_gain_dB_value();

char lc75341_treble_dB(signed char treble);
char lc75341_treble_up();
char lc75341_treble_down();
signed char lc75341_treble_dB_value();

char lc75341_bass_dB(unsigned char bass);
char lc75341_bass_up();
char lc75341_bass_down();
unsigned char lc75341_bass_dB_value();

void lc75341_reset_equaliuzer();

#endif /* LC75341_H_ */