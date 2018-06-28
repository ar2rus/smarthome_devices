#include <inttypes.h>
#include "lc75341.h"



/******************************\
 *        Constructor         *
 *  just set class variables  *
\******************************/
lc75341::lc75341(uint8_t di_pin, uint8_t cl_pin, uint8_t ce_pin) {
   ccb = new SanyoCCB(0, cl_pin, di_pin, ce_pin);
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
	char input = registry[0] & 0x0F;
	input++;
	if (input > LC75341_INPUT_4){
		input = LC75341_INPUT_1;
	}
	return input(input);
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

char lc75341::volume(char volume){
	if (volume >= LC75341_VOLUME_MAX && volume <= LC75341_VOLUME_MIN){
		if (volume_value() != volume){
			registry[1] = volume;
			write();
		}
		return 1;
	}
	return 0;
}



/*
*	������������� ������� ��������� �� �������� � ��������� (0 dB ... -80 dB)
*/
char lc75341::volume_dB(signed char volume){
	return volume(-volume);
}

/*
*	������������� ������� ��������� �� �������� � ��������� �� ������������(0-100 %)
*/
char lc75341::volume_percent(unsigned char volume){
	if (volume <= 100){
		volume = LC75341_VOLUME_MIN - LC75341_VOLUME_MIN * volume / 100;
		return volume(volume);
	}
	return 0;
}

/*
*	����������� ������� ���������� �� step dB
*/
char lc75341::volume_up(char step){
	unsigned char volume = volume_value();
	
	/* try to UNMUTE at first*/
	if (unmute()){
		return 1;
	}else{
		if (volume > LC75341_VOLUME_MAX){
			if ((volume - step) > LC75341_VOLUME_MAX){
				volume -= step;
			}else{
				volume = LC75341_VOLUME_MAX;
			}
			return volume(volume);
		}
	}
	return 0;
}

/*
*	��������� ������� ���������� �� step dB
*/
char lc75341::volume_down(char step){
	unsigned char volume = volume_value();
	
	/* try to UNMUTE at first*/
	if (unmute()){
		return 1;
	}else{
		if (volume < LC75341_VOLUME_MIN){
			if ((volume + step) < LC75341_VOLUME_MIN){
				volume += step;
			}else{
				volume = LC75341_VOLUME_MIN;
			}
			return volume(volume);
		}
	}
	return 0;
}


/*
*	��������������� ����������� ������� ���������� �� step dB
*/
char lc75341::volume_up_exp(unsigned char step){
	unsigned char v = volume_value();
	v /= 10;
	
	return volume_up(exp_koeff[v]*step);
}

/*
*	��������������� ��������� ������� ���������� �� step dB
*/
char lc75341::volume_down_exp(unsigned char step){
	unsigned char v = volume_value();
	v /= 10;
	
	return volume_down(exp_koeff[v]*step);
}


/*
*	��������� ����
*/
char lc75341::mute(){
	unsigned char volume = volume_value();
	if (volume < LC75341_VOLUME_MIN){
		muted_volume = volume;
		return volume(LC75341_VOLUME_MIN);
	}
	return 0;
}

/*
*	�������� ����, ���� �� ����� �� ��� �������� �������� lc75341_mute
*/
char lc75341::unmute(){
	if (muted_volume < LC75341_VOLUME_MIN){
		if (volume(muted_volume)){
			muted_volume = LC75341_VOLUME_MIN;
			return 1;
		}
	}
	return 0;
}


/*
*	������������ ����� lc75341_mute � lc75341_unmute
*/
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

char lc75341::gain(char gain){
	if (gain >= LC75341_GAIN_MIN && gain <= LC75341_GAIN_MAX){
		if (gain != gain_value()){
			registry[0] = (registry[0] & 0x0F) | (gain<<4);
			write();
		}
		return 1;
	}
	return 0;
}


/*
*	�������� �������� ������� �� gain dB
*/
char lc75341::gain_dB(unsigned char gain){
	return gain(gain / 2);
}

/*
*	���������� �������� �������� ������� �� 2 dB
*/
char lc75341::gain_up(){
	return gain_dB(gain_dB_value() + 2);
}

/*
*	���������� �������� �������� ������� �� 2 dB
*/
char lc75341::gain_down(){
	return gain_dB(gain_dB_value() - 2);
}

signed char lc75341::treble_value(){
	signed char treble = registry[2] & 0x0F;
	if (treble & 0x08){ //minus dB treble
		treble = -(treble & 0x07);
	}
	return treble;
}

signed char lc75341::treble_dB_value(){
	return treble_value() * 2;
}

char lc75341::treble(signed char treble){
	if (treble >= LC75341_TREBLE_MIN && treble <= LC75341_TREBLE_MAX){
		if (treble != treble_value()){
			if (treble < 0){
				treble = (-treble) | 0x08;
			}
			registry[2] = (registry[2] & 0xF0) | treble;
			write();
		}
		return 1;
	}
	return 0;
}



/*
*	������������� �������� ����������� ������� ������ � �� (-10 �� ... +10��)
*/
char lc75341::treble_dB(signed char treble){
	return treble(treble / 2);
}

/*
*	����������� �������� ����������� ������� ������ �� 2 ��
*/
char lc75341::treble_up(){
	return treble_dB(treble_dB_value() + 2);
}

/*
*	��������� �������� ����������� ������� ������ �� 2 ��
*/
char lc75341::treble_down(){
	return treble_dB(treble_dB_value() - 2);
}


unsigned char lc75341::bass_value(){
	return (registry[2] & 0xF0) >> 4;
}

unsigned char lc75341::bass_dB_value(){
	return bass_value() * 2;
}

char lc75341::bass(unsigned char bass){
	if (bass >= LC75341_BASS_MIN && bass <= LC75341_BASS_MAX){
		if (bass != bass_value()){
			registry[2] = (registry[2] & 0x0F) | (bass<<4);
			write();
		}
		return 1;
	}
	return 0;
}

/*
*	������������� �������� ����������� ������ ������ � �� (0 �� ... +20��)
*/
char lc75341::bass_dB(unsigned char bass){
	return bass(bass / 2);
}

/*
*	����������� �������� ����������� ������ ������ �� 2 ��
*/
char lc75341::bass_up(){
	return bass_dB(bass_dB_value() + 2);
}

/*
*	��������� �������� ����������� ������ ������ �� 2 ��
*/
char lc75341::bass_down(){
		return bass_dB(bass_dB_value() - 2);
}

/*
*	���������� �������� ����������� (���.�������, ���.�������, ��������)
*/
void lc75341::equalizer_reset(){
		registry[0] = registry[0] & 0x0F;
		registry[2] = 0x00;
		write();
}
