#ifndef lc75341_h
#define lc75341_h

#include <inttypes.h>
#include "Arduino.h"
#include "SanyoCCB.h"


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

static const char exp_koeff[9]= {1, 1, 1, 1, 2, 2, 4, 4, 8};

class lc75341 {
	public:
		lc75341(uint8_t, uint8_t, uint8_t);
    void init();
    
		char input(unsigned char input);
		char input_next();
		char input_prev();
		unsigned char input_value();

		char volume_dB(signed char volume);
		char volume_percent(unsigned char volume);
		char volume_up(char step);
		char volume_down(char step);

		char volume_up_exp(unsigned char step);
		char volume_down_exp(unsigned char step);

		char mute();
		char unmute();
		void mute_toggle();

		signed char volume_dB_value();
		unsigned char volume_percent_value();


		char gain_dB(unsigned char gain);
		char gain_up();
		char gain_down();
		unsigned char gain_dB_value();

		char treble_dB(signed char treble);
		char treble_up();
		char treble_down();
		signed char treble_dB_value();

		char bass_dB(unsigned char bass);
		char bass_up();
		char bass_down();
		unsigned char bass_dB_value();

		void equalizer_reset();
	private:
		void write();
		SanyoCCB* ccb;
		char muted_volume = LC75341_VOLUME_MIN;
		unsigned char registry[4] = {0x00, 0x00, 0x00, 0x0C};
};

#endif
