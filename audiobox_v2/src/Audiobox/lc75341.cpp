#include <inttypes.h>
#include "lc75341.h"



/******************************\
 *        Constructor         *
 *  just set class variables  *
\******************************/
lc75341::lc75341(uint8_t do_pin, uint8_t cl_pin, uint8_t ce_pin) {
   ccb = new SanyoCCB(do_pin, cl_pin, ce_pin);
}

/******************************************\
 *                 init()                 *
 *  Set pin functions and initial states  *
\******************************************/ 
void lc75341::init() {
	ccb->init();
}

void lc75341::write(){
	ccb->write(LC75341_ADDRESS, registry, sizeof(registry));
}

unsigned char lc75341::input_value(){
	return registry[0] & 0x03;
}


char lc75341::input(unsigned char input){
	if (input >= LC75341_INPUT_1 && input <= LC75341_INPUT_4){
		if (input_value() != input){	//call write if need to set a new value only
			registry[0] = (registry[0] & 0xF0) | input;
			write();
		}
		return 1;
	}
	return 0;
}

char lc75341::input_next(){
	char i = registry[0] & 0x0F;
	i++;
	if (i > LC75341_INPUT_4){
		i = LC75341_INPUT_1;
	}
	return input(i);
}

char lc75341::input_prev(){
	signed char i = registry[0] & 0x0F;
	i--;
	if (i < LC75341_INPUT_1){
		i = LC75341_INPUT_4;
	}
	return input(i);
}


unsigned char lc75341::volume_value(){
	return registry[1];
}

signed char lc75341::volume_dB_value(){
	return -volume_value();
}

unsigned char lc75341::volume_percent_value(){
	return 100 - 100 * volume_value() / LC75341_VOLUME_MIN;
}

char lc75341::volume(char v){
	if (v >= LC75341_VOLUME_MAX && v <= LC75341_VOLUME_MIN){
		if (volume_value() != v){
			registry[1] = v;
			write();
		}
		return 1;
	}
	return 0;
}


char lc75341::volume_dB(signed char v){
	return volume(-v);
}

char lc75341::volume_percent(unsigned char v){
	if (v <= 100){
		v = LC75341_VOLUME_MIN - LC75341_VOLUME_MIN * v / 100;
		return volume(v);
	}
	return 0;
}

char lc75341::volume_up(char step){
	unsigned char v = volume_value();
	
	/* try to UNMUTE at first*/
	if (unmute()){
		return 1;
	}else{
		if (v > LC75341_VOLUME_MAX){
			if ((v - step) > LC75341_VOLUME_MAX){
				v -= step;
			}else{
				v = LC75341_VOLUME_MAX;
			}
			return volume(v);
		}
	}
	return 0;
}

char lc75341::volume_down(char step){
	unsigned char v = volume_value();
	
	/* try to UNMUTE at first*/
	if (unmute()){
		return 1;
	}else{
		if (v < LC75341_VOLUME_MIN){
			if ((v + step) < LC75341_VOLUME_MIN){
				v += step;
			}else{
				v = LC75341_VOLUME_MIN;
			}
			return volume(v);
		}
	}
	return 0;
}


char lc75341::volume_up_exp(unsigned char step){
	unsigned char v = volume_value();
	v /= 10;
	
	return volume_up(exp_koeff[v]*step);
}

char lc75341::volume_down_exp(unsigned char step){
	unsigned char v = volume_value();
	v /= 10;
	
	return volume_down(exp_koeff[v]*step);
}


char lc75341::mute(){
	unsigned char v = volume_value();
	if (v < LC75341_VOLUME_MIN){
		muted_volume = v;
		return volume(LC75341_VOLUME_MIN);
	}
	return 0;
}

char lc75341::unmute(){
	if (muted_volume < LC75341_VOLUME_MIN){
		if (volume(muted_volume)){
			muted_volume = LC75341_VOLUME_MIN;
			return 1;
		}
	}
	return 0;
}


void lc75341::mute_toggle(){
	if (!unmute()){
		mute();
	}
}

unsigned char lc75341::gain_value(){
	return (registry[0] & 0xF0) >> 4;
}

unsigned char lc75341::gain_dB_value(){
	return gain_value() * 2;
}

char lc75341::gain(char g){
	if (g >= LC75341_GAIN_MIN && g <= LC75341_GAIN_MAX){
		if (g != gain_value()){
			registry[0] = (registry[0] & 0x0F) | (g<<4);
			write();
		}
		return 1;
	}
	return 0;
}


char lc75341::gain_dB(unsigned char g){
	return gain(g / 2);
}

char lc75341::gain_up(){
	return gain_dB(gain_dB_value() + 2);
}

char lc75341::gain_down(){
	return gain_dB(gain_dB_value() - 2);
}

signed char lc75341::treble_value(){
	signed char t = registry[2] & 0x0F;
	if (t & 0x08){ //minus dB treble
		t = -(t & 0x07);
	}
	return t;
}

signed char lc75341::treble_dB_value(){
	return treble_value() * 2;
}

char lc75341::treble(signed char t){
	if (t >= LC75341_TREBLE_MIN && t <= LC75341_TREBLE_MAX){
		if (t != treble_value()){
			if (t < 0){
				t = (-t) | 0x08;
			}
			registry[2] = (registry[2] & 0xF0) | t;
			write();
		}
		return 1;
	}
	return 0;
}


char lc75341::treble_dB(signed char t){
	return treble(t / 2);
}

char lc75341::treble_up(){
	return treble_dB(treble_dB_value() + 2);
}

char lc75341::treble_down(){
	return treble_dB(treble_dB_value() - 2);
}


unsigned char lc75341::bass_value(){
	return (registry[2] & 0xF0) >> 4;
}

unsigned char lc75341::bass_dB_value(){
	return bass_value() * 2;
}

char lc75341::bass(unsigned char b){
	if (b >= LC75341_BASS_MIN && b <= LC75341_BASS_MAX){
		if (b!= bass_value()){
			registry[2] = (registry[2] & 0x0F) | (b<<4);
			write();
		}
		return 1;
	}
	return 0;
}

char lc75341::bass_dB(unsigned char b){
	return bass(b / 2);
}

char lc75341::bass_up(){
	return bass_dB(bass_dB_value() + 2);
}

char lc75341::bass_down(){
		return bass_dB(bass_dB_value() - 2);
}

void lc75341::equalizer_reset(){
		registry[0] = registry[0] & 0x0F;
		registry[2] = 0x00;
		write();
}
