
#include "lc75341.h"

#include <avr/io.h>
#include <util/delay.h>


/******************************************\
 *                 init()                 *
 *  Set pin functions and initial states  *
\******************************************/ 
void ccb_init(){
	set_bit(DDRPORT(LC75341_DO_PORT), LC75341_DO_PIN);		//do - output
	set_bit(DDRPORT(LC75341_CE_PORT), LC75341_CE_PIN);		//ce - output
	set_bit(DDRPORT(LC75341_CL_PORT), LC75341_CL_PIN);		//cl - output
	
	unset_bit(OUTPORT(LC75341_DO_PORT), LC75341_DO_PIN);	//do - low
	unset_bit(OUTPORT(LC75341_CL_PORT), LC75341_CL_PIN);	//cl - low (Clock-rest-low mode)
	
	// Paranoia: cycle CE to "flush" de bus
	set_bit(OUTPORT(LC75341_CE_PORT), LC75341_CE_PIN);
	_delay_us(CCB_DELAY);
	unset_bit(OUTPORT(LC75341_CE_PORT), LC75341_CE_PIN);
	_delay_us(CCB_DELAY);
}

/************************************\
 *           writeByte()            *
 *  Send a single byte via CCB bus  *
\************************************/ 
void ccb_write_byte(char data) {
	// Send one byte out via CCB bus (LSB first)
	for(char i = 0; i < 8; i++) {
		if ((data>>i) & 1){
			set_bit(OUTPORT(LC75341_DO_PORT), LC75341_DO_PIN);
		}else{
			unset_bit(OUTPORT(LC75341_DO_PORT), LC75341_DO_PIN);
		}
		
		set_bit(OUTPORT(LC75341_CL_PORT), LC75341_CL_PIN);
		_delay_us(CCB_DELAY);
		unset_bit(OUTPORT(LC75341_CL_PORT), LC75341_CL_PIN);
		_delay_us(CCB_DELAY);
	}
}

/********************************************************\
 *                     write()                          *
 *  Send dataLength (up to 127) bytes via CCB bus       *
 * Note: the contents of the input buffer is send       *
 * backwards (from the rightmost to the leftmost byte), *
 * so the order of the data bytes must be the opposite  *
 * as the one shown on the device's datasheets          *
\********************************************************/ 
void ccb_write(char address, char *data, char dataLength){
	// Send the address, with the nibbles swapped (required by the CCB protocol to support 4-bit addresses)
	//ccb_write_byte((address >> 4) | (address << 4));
	ccb_write_byte(address);
  
	// Enter the data transfer mode
	unset_bit(OUTPORT(LC75341_CL_PORT), LC75341_CL_PIN);
	set_bit(OUTPORT(LC75341_CE_PORT), LC75341_CE_PIN);
	_delay_us(CCB_DELAY);
 
	// Send data
	// Note: as CCB devices usually reads registers data from MSB to LSB, the buffer is read from left to right
	for(unsigned char i = 0; i < dataLength; i++)
		ccb_write_byte(data[i]);
	unset_bit(OUTPORT(LC75341_DO_PORT), LC75341_DO_PIN);

	unset_bit(OUTPORT(LC75341_CE_PORT), LC75341_CE_PIN);
	_delay_us(CCB_DELAY);
}


/**data to write */
char data[4] = { 0x0, 0x0, 0x0, 0xC };

/** saved volume value for mute/unmute **/
char muted_volume = LC75341_VOLUME_MIN;


void lc75341_write(){
	ccb_write(LC75341_ADDRESS, data, 4);
}

void lc75341_init(){
	ccb_init();   
	 _delay_ms(50);  //???
}

unsigned char lc75341_input_value(){
	return data[0] & 0x03;
}

char lc75341_input(unsigned char input){
	if (input >= LC75341_INPUT_1 && input <= LC75341_INPUT_4){
		if (lc75341_input_value() != input){	//call write if need to set a new value only
			data[0] = (data[0] & 0xF0) | input;
			lc75341_write();
		}
		return 1;
	}
	return 0;
}

char lc75341_input_next(){
	char input = data[0] & 0x0F;
	input++;
	if (input > LC75341_INPUT_4){
		input = LC75341_INPUT_1;
	}
	return lc75341_input(input);
}

char lc75341_input_prev(){
	signed char input = data[0] & 0x0F;
	input--;
	if (input < LC75341_INPUT_1){
		input = LC75341_INPUT_4;
	}
	return lc75341_input(input);
}


unsigned char lc75341_volume_value(){
	return data[1];
}

signed char lc75341_volume_dB_value(){
	return -lc75341_volume_value();
}

unsigned char lc75341_volume_percent_value(){
	return 100 - 100 * lc75341_volume_value() / LC75341_VOLUME_MIN;
}

char lc75341_volume(char volume){
	if (volume >= LC75341_VOLUME_MAX && volume <= LC75341_VOLUME_MIN){
		if (lc75341_volume_value() != volume){
			data[1] = volume;
			lc75341_write();
		}
		return 1;
	}
	return 0;
}



/*
*	”станавливает уровень громкости по значению в децибелах (0 dB ... -80 dB)
*/
char lc75341_volume_dB(signed char volume){
	return lc75341_volume(-volume);
}

/*
*	”станавливает уровень громкости по значению в процентах от максимальной(0-100 %)
*/
char lc75341_volume_percent(unsigned char volume){
	volume = LC75341_VOLUME_MIN - LC75341_VOLUME_MIN * volume / 100;
	return lc75341_volume(volume);
}

/*
*	”величивает уровень громокости на step dB
*/
char lc75341_volume_up(char step){
	unsigned char volume = lc75341_volume_value();
	
	/* try to UNMUTE at first*/
	if (lc75341_unmute()){
		return 1;
	}else{
		if (volume > LC75341_VOLUME_MAX){
			if ((volume - step) > LC75341_VOLUME_MAX){
				volume -= step;
			}else{
				volume = LC75341_VOLUME_MAX;
			}
			return lc75341_volume(volume);
		}
	}
	return 0;
}

/*
*	”меньшает уровень громокости на step dB
*/
char lc75341_volume_down(char step){
	unsigned char volume = lc75341_volume_value();
	
	/* try to UNMUTE at first*/
	if (lc75341_unmute()){
		return 1;
	}else{
		if (volume < LC75341_VOLUME_MIN){
			if ((volume + step) < LC75341_VOLUME_MIN){
				volume += step;
			}else{
				volume = LC75341_VOLUME_MIN;
			}
			return lc75341_volume(volume);
		}
	}
	return 0;
}



const char exp_koeff[]= {1, 1, 1, 2, 2, 4, 8, 8, 16};


/*
*	Ёкспоненциально увеличивает уровень громокости на step dB
*/
char lc75341_volume_up_exp(unsigned char step){
	unsigned char v = lc75341_volume_value();
	v /= 10;
	
	return lc75341_volume_up(exp_koeff[v]*step);
}

/*
*	Ёкспоненциально уменьшает уровень громокости на step dB
*/
char lc75341_volume_down_exp(unsigned char step){
	unsigned char v = lc75341_volume_value();
	v /= 10;
	
	return lc75341_volume_down(exp_koeff[v]*step);
}


/*
*	ќтключает звук
*/
char lc75341_mute(){
	unsigned char volume = lc75341_volume_value();
	if (volume < LC75341_VOLUME_MIN){
		muted_volume = volume;
		return lc75341_volume(LC75341_VOLUME_MIN);
	}
	return 0;
}

/*
*	¬ключает звук, если он ранее он был отключен командой lc75341_mute
*/
char lc75341_unmute(){
	if (muted_volume < LC75341_VOLUME_MIN){
		if (lc75341_volume(muted_volume)){
			muted_volume = LC75341_VOLUME_MIN;
			return 1;
		}
	}
	return 0;
}


/*
*	ѕереключение между lc75341_mute и lc75341_unmute
*/
void lc75341_mute_toggle(){
	if (!lc75341_unmute()){
		lc75341_mute();
	}
}

unsigned char lc75341_gain_value(){
	return (data[0] & 0xF0) >> 4;
}

unsigned char lc75341_gain_dB_value(){
	return lc75341_gain_value() * 2;
}

char lc75341_gain(char gain){
	if (gain >= LC75341_GAIN_MIN && gain <= LC75341_GAIN_MAX){
		if (gain != lc75341_gain_value()){
			data[0] = (data[0] & 0x0F) | (gain<<4);
			lc75341_write();
		}
		return 1;
	}
	return 0;
}


/*
*	”силение входного сигнала на gain dB
*/
char lc75341_gain_dB(unsigned char gain){
	return lc75341_gain(gain / 2);
}

/*
*	”величение усилени€ входного сигнала на 2 dB
*/
char lc75341_gain_up(){
	return lc75341_gain_dB(lc75341_gain_dB_value() + 2);
}

/*
*	”меньшение усилени€ входного сигнала на 2 dB
*/
char lc75341_gain_down(){
	return lc75341_gain_dB(lc75341_gain_dB_value() - 2);
}

signed char lc75341_treble_value(){
	signed char treble = data[2] & 0x0F;
	if (treble & 0x08){ //minus dB treble
		treble = -(treble & 0x07);
	}
	return treble;
}

signed char lc75341_treble_dB_value(){
	return lc75341_treble_value() * 2;
}

char lc75341_treble(signed char treble){
	if (treble >= LC75341_TREBLE_MIN && treble <= LC75341_TREBLE_MAX){
		if (treble != lc75341_treble_value()){
			if (treble < 0){
				treble = (-treble) | 0x08;
			}
			data[2] = (data[2] & 0xF0) | treble;
			lc75341_write();
		}
		return 1;
	}
	return 0;
}



/*
*	”станавливает значение эквалайзера высоких частот в дЅ (-10 дЅ ... +10дЅ)
*/
char lc75341_treble_dB(signed char treble){
	return lc75341_treble(treble / 2);
}

/*
*	”величивает значение эквалайзера высоких частот на 2 дЅ
*/
char lc75341_treble_up(){
	return lc75341_treble_dB(lc75341_treble_dB_value() + 2);
}

/*
*	”меньшает значение эквалайзера высоких частот на 2 дЅ
*/
char lc75341_treble_down(){
	return lc75341_treble_dB(lc75341_treble_dB_value() - 2);
}


unsigned char lc75341_bass_value(){
	return (data[2] & 0xF0) >> 4;
}

unsigned char lc75341_bass_dB_value(){
	return lc75341_bass_value() * 2;
}

char lc75341_bass(unsigned char bass){
	if (bass >= LC75341_BASS_MIN && bass <= LC75341_BASS_MAX){
		if (bass != lc75341_bass_value()){
			data[2] = (data[2] & 0x0F) | (bass<<4);
			lc75341_write();
		}
		return 1;
	}
	return 0;
}

/*
*	”станавливает значение эквалайзера низких частот в дЅ (0 дЅ ... +20дЅ)
*/
char lc75341_bass_dB(unsigned char bass){
	return lc75341_bass(bass / 2);
}

/*
*	”величивает значение эквалайзера низких частот на 2 дЅ
*/
char lc75341_bass_up(){
	return lc75341_bass_dB(lc75341_bass_dB_value() + 2);
}

/*
*	”меньшает значение эквалайзера низких частот на 2 дЅ
*/
char lc75341_bass_down(){
		return lc75341_bass_dB(lc75341_bass_dB_value() - 2);
}

/*
*	—брасывает значени€ эквалайзера (выс.частоты, низ.частоты, усиление)
*/
void lc75341_reset_equalizer(){
		data[0] = data[0] & 0x0F;
		data[2] = 0x00;
		lc75341_write();
}