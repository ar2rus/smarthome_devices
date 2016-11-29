/*
 * heatfloor_settings.c
 *
 * Created: 12.10.2015 17:56:40
 *  Author: gargon
 */ 

#include "heatfloor.h"

#include <avr/eeprom.h>
#include <string.h>

void (*on_heatfloor_request_systime)(void (*hf_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)) = 0;

void (*on_heatfloor_channel_modes_changed)(heatfloor_channel_modes* modes) = 0;
void (*on_heatfloor_program_changed)(heatfloor_program* program) = 0;


heatfloor_datetime time;

heatfloor_channel_modes modes;
heatfloor_program programs_cache[HEATFLOOR_CHANNELS_COUNT];	//for cache

heatfloor_program program_tmp;


unsigned char is_time_valid(){
	return time.day_of_week;
}

void heatfloor_reset_programs_cache(){
	//resets channel program cache
	for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
		programs_cache[i].program_num = 0xFF;
	}
}

void heatfloor_dispatcher_set_systime(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week){
	time.seconds = seconds;
	time.minutes = minutes;
	time.hours = hours;
	time.day_of_week = day_of_week;
}


heatfloor_datetime* heatfloor_systime(){
	return &time;
}


heatfloor_channel_modes* heatfloor_modes_info(){
	return &modes;
}

//program num: 0-9
unsigned char heatfloor_program_values_read(unsigned char program_num, heatfloor_program_values* program_values){
	if (program_num >=0 && program_num < HEATFLOOR_PROGRAMS_COUNT){
		eeprom_read_block(program_values, (void *)EEPROM_ADDRESS_HEATFLOOR_PROGRAMS + program_num*sizeof(heatfloor_program_values), sizeof(heatfloor_program_values));
		return 1;
	}
	return 0;
}

//program_num: F0-F9
heatfloor_program* heatfloor_program_info(unsigned char program_num){
	if (heatfloor_program_values_read(program_num & 0x0F, &program_tmp.program)){
		program_tmp.program_num = program_num;
	}else{
		program_tmp.program_num = 0xFF;
	}
	
	return &program_tmp;
}

void requestSystime(){
	if (on_heatfloor_request_systime){
		(*on_heatfloor_request_systime)( heatfloor_dispatcher_set_systime );
	}
}

signed int heatfloor_dispatcher_resolve_temperature_setting(unsigned char channel){
	
	if (is_time_valid()){
		
		switch (modes.channels[channel].mode){
			case HEATFLOOR_MODE_OFF:
				return -1;							//отключен (режим)
			case HEATFLOOR_MODE_MANUAL:
			case HEATFLOOR_MODE_PARTY:
				return modes.channels[channel].t * 10;
			case HEATFLOOR_MODE_DAY:
			case HEATFLOOR_MODE_DAY_FOR_TODAY:
			case HEATFLOOR_MODE_WEEK:{
					unsigned char program_num = modes.channels[channel].p;
					if (modes.channels[channel].mode == HEATFLOOR_MODE_WEEK){
						switch (time.day_of_week){
							case 6:
								program_num = modes.channels[channel].p_sa_su[HEATFLOOR_PROGRAM_SA];
								break;
							case 7:
								program_num = modes.channels[channel].p_sa_su[HEATFLOOR_PROGRAM_SU];
								break;
						}
					}
					
					if (program_num < HEATFLOOR_PROGRAMS_COUNT){
						//если в кэше нет программы - то выгружаем туда из eeprom
						if (programs_cache[channel].program_num != program_num){
							//загружаем в кэш
							heatfloor_program_values_read(program_num, &programs_cache[channel].program);
							programs_cache[channel].program_num = program_num;
						}
						heatfloor_program_values p = programs_cache[channel].program;
				
						if (p.num_values > 0 && p.num_values < HEATFLOOR_PROGRAMS_COUNT){
							unsigned char i = p.num_values - 1;
							while (i > 0){
								if (time.hours >= p.values[i].hour){
									break;
								}
								i--;
							}
							return p.values[i].t * 10;
						}
					}
				}
				break;
			//default:
			//	return INT16_MIN;	//неизвестный режим (ошибка диспетчера)
		}
	}
	return INT16_MIN; //диспетчер не проинициализирован (ошибка диспетчера)
}

void loadDefaultMode(unsigned char channel){
	if (channel < HEATFLOOR_CHANNELS_COUNT){
		eeprom_read_block(&modes.channels[channel], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + channel*sizeof(heatfloor_channel_mode), sizeof(heatfloor_channel_mode));
	}
}

void heatfloor_dispatcher_tick_second(){
	
	if (is_time_valid()){
		if (++time.seconds == 60){
			time.seconds = 0;
			if (++time.minutes == 60){
				time.minutes = 0;
				if (++time.hours == 24){
					time.hours = 0;
					if (++time.day_of_week == 8){
						time.day_of_week = 1;
					}
					
					//reset day_for_today programs after 00:00 o'clock
					for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
						if (modes.channels[i].mode == HEATFLOOR_MODE_DAY_FOR_TODAY){
							loadDefaultMode(i);
						}
					}
				}
				requestSystime();	//корректируем время каждый час
			}
		}
		
		//dec timers on party modes, if exists
		for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
			if (modes.channels[i].mode == HEATFLOOR_MODE_PARTY){
				if (!(--modes.channels[i].timer)){
					loadDefaultMode(i);
				}
			}
		}
		
	}else{
		requestSystime();
	}
}


/************************************************************************/
/* Устанавливает режим для канала(каналов)                              */
/************************************************************************/
//  0-байт: битовая маска каналов, на которые распространяется команда
//	1-байт: режим
//		0 - выкл
//		1 - ручной (с указанием поддерживаемой t*)
//		2 - дневной (с указанием номера программы)
//		3 - недельный (с указанием 7 программ на каждый день)
//		4 - вечеринка (с указанием поддерживаемой t* и времени в секундах)
//		5 - дневной на сегодня (с указанием программы)
//  2-4 байты: необходимые доп.параметры для режимов

unsigned char heatfloor_dispatcher_set_mode(char* data, char size){
	signed char r = 0;
	if (size > 1){
		
		unsigned char channels = data[0];
		unsigned char mode = data[1];
		
		//check size
		switch(mode){
			case HEATFLOOR_MODE_OFF:
				r = size == 2;
				break;
			case HEATFLOOR_MODE_MANUAL:
			case HEATFLOOR_MODE_DAY:
			case HEATFLOOR_MODE_DAY_FOR_TODAY:
				r = size == 3;
				break;
			case HEATFLOOR_MODE_WEEK:
			case HEATFLOOR_MODE_PARTY:
				r = size == 5;
				break;
		}
		
		if (r){
			r = 0;
			for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
				if (test_bit(channels, i)){
					
					memcpy(&modes.channels[i], &data[1], size-1);	//копируем все как есть без первого байта - маски каналов
					
					//определяем режимы, при которых сохраняем в eeprom
					switch (mode){
						case HEATFLOOR_MODE_OFF:
						case HEATFLOOR_MODE_MANUAL:
						case HEATFLOOR_MODE_DAY:
						case HEATFLOOR_MODE_WEEK:
							eeprom_busy_wait();
							eeprom_update_block(&modes.channels[i], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + (i*sizeof(heatfloor_channel_mode)), sizeof(heatfloor_channel_mode));
							break;
					}
					r = 1;
				}
			}
			
			if (r){
				//сообщение об изменении режима
				if (on_heatfloor_channel_modes_changed){
					(*on_heatfloor_channel_modes_changed)(&modes);
				}
			}
			
		}
	}
	return r;
}


/************************************************************************/
/* Записывает программу в EEPROM и начинает ее использовать				*/
/************************************************************************/
//	1-байт: номер программы (F0-F9)
//  2-20-байт: до 10 пар значений: час - температура

unsigned char heatfloor_dispatcher_set_program(char* data, char size){
	unsigned char r = 0;
	if (size > 2){
		
		unsigned char program_num = data[0] & 0x0F;
		
		if (program_num >=0 && program_num < HEATFLOOR_PROGRAMS_COUNT){	//номер программы
			r = 1;
			
			program_tmp.program_num = data[0];		//original 0xF0 masked for putting in response
			program_tmp.program.num_values = 0;
			
			signed char h = -1;
			for (int i=1; i<size; i+=2){
				if (data[i] <= h || data[i] > 23){	//нарушен порядок часов в программе, или указан неверный час
					r= 0;
					break;
				}else{
					h = data[i];
				}
				
				program_tmp.program.num_values++;
			}
			
			//если программа корректная -> сохраняем в eeprom
			if (r){
				
				memcpy(&program_tmp.program.values, &data[1], program_tmp.program.num_values * sizeof(heatfloor_program_value));
				
				//write
				eeprom_busy_wait();
				eeprom_update_block(&program_tmp.program, (void *)EEPROM_ADDRESS_HEATFLOOR_PROGRAMS + (program_num * sizeof(heatfloor_program_values)), sizeof(heatfloor_program_values));
				
				heatfloor_reset_programs_cache();
				
				//сообщение об изменении программы
				if (on_heatfloor_program_changed){
					(*on_heatfloor_program_changed)(&program_tmp);
				}
			}
		}
	}
	return r;
}

unsigned char heatfloor_dispatcher_command(char* data, char size){
	if (size > 1){
		switch (data[0]){
			case HEATFLOOR_MESSAGE_MODES_PREAMBULE:	//0xFE - установка режимов
				return heatfloor_dispatcher_set_mode(&data[1], size-1);		//skip 0xFE byte
			case HEATFLOOR_MESSAGE_PROGRAM_PREAMBULE ... (HEATFLOOR_MESSAGE_PROGRAM_PREAMBULE | (HEATFLOOR_PROGRAMS_COUNT-1)):	//0xF0-0xF9 - установка программ
				return heatfloor_dispatcher_set_program(data, size);	
		}
	}
	return 0;
}

void heatfloor_set_on_modes_changed(void(*f)(heatfloor_channel_modes* modes_)){
	on_heatfloor_channel_modes_changed = f;
}

void heatfloor_set_on_program_changed(void(*f)(heatfloor_program* program)){
	on_heatfloor_program_changed = f;
}

void heatfloor_dispatcher_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week))){
	modes.type = HEATFLOOR_MESSAGE_MODES_PREAMBULE;
	
	eeprom_read_block(&modes.channels, (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES, HEATFLOOR_CHANNELS_COUNT * sizeof(heatfloor_channel_mode));
	heatfloor_reset_programs_cache();
	
	time.day_of_week = 0;	//undefined time
	on_heatfloor_request_systime = f_request_systime;
	
	//requestSystime();
}