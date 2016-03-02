/*
 * heatfloor_settings.c
 *
 * Created: 12.10.2015 17:56:40
 *  Author: gargon
 */ 

#include "heatfloor.h"

#include <avr/eeprom.h>
#include <string.h>

void (*on_heatfloor_dispather_request_systime)(void (*hf_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)) = 0;

void (*on_heatfloor_channel_modes_changed)(heatfloor_channel_mode* modes) = 0;
void (*on_heatfloor_program_changed)(unsigned char program_num, heatfloor_program* program) = 0;

heatfloor_datetime time;

char channel_modes_buf[HEATFLOOR_CHANNELS_COUNT * sizeof(heatfloor_channel_mode)];
char channel_programs_buf[HEATFLOOR_CHANNELS_COUNT * sizeof(heatfloor_channel_program)];

char program_buf[sizeof(heatfloor_program)];


unsigned char is_time_valid(){
	return time.day_of_week;
}

void heatfloor_clear_channel_programs_cache(){
	//reset channel program cache
	heatfloor_channel_program* cpc = (heatfloor_channel_program*)(&channel_programs_buf[0]);

	for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
		cpc->program_num = -1;
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

void requestSystime(){
	if (on_heatfloor_dispather_request_systime){
		(*on_heatfloor_dispather_request_systime)( heatfloor_dispatcher_set_systime );
	}
}

signed int heatfloor_dispatcher_resolve_temperature_setting(unsigned char channel){
	
	if (is_time_valid()){
		heatfloor_channel_mode* cm = (heatfloor_channel_mode*)(&channel_modes_buf[0]);
		heatfloor_channel_program* cpc = (heatfloor_channel_program*)(&channel_programs_buf[0]);
		
		switch (cm[channel].mode){
			case HEATFLOOR_MODE_OFF:
				return -1;							//отключен (режим)
			case HEATFLOOR_MODE_MANUAL:
			case HEATFLOOR_MODE_PARTY:
				return cm[channel].t * 10;
			case HEATFLOOR_MODE_DAY:
			case HEATFLOOR_MODE_DAY_FOR_TODAY:
			case HEATFLOOR_MODE_WEEK:
				{
					unsigned char program_num = cm[channel].p;
					if (cm[channel].mode == HEATFLOOR_MODE_WEEK){
						switch (time.day_of_week){
							case 6:
								program_num = cm[channel].p_sa_su[PROGRAM_SA];
								break;
							case 7:
								program_num = cm[channel].p_sa_su[PROGRAM_SU];
								break;
						}
					}
				
					if (program_num < 10){
						//если в кэше нет программы - то выгружаем туда из eeprom
						if (cpc[channel].program_num != program_num){
							//сохраняем в кэш
							memcpy(&cpc[channel].program, heatfloor_program_info(program_num), sizeof(heatfloor_program));
							cpc[channel].program_num = program_num;
						}
						heatfloor_program* p = &cpc[channel].program;
				
						if (p->num_values > 0 && p->num_values < 10){
							unsigned char i = p->num_values - 1;
							while (i > 0){
								if (time.hours >= p->values[i].hour){
									break;
								}
								i--;
							}
							return p->values[i].t * 10;
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

void heatfloor_dispatcher_tick_second(){
	
	heatfloor_channel_mode* cm = (heatfloor_channel_mode*)(&channel_modes_buf[0]);
	
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
					
					//reset day_for_today programs
					for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
						if (cm[i].mode == HEATFLOOR_MODE_DAY_FOR_TODAY){
							//load default mode from eeprom
							eeprom_read_block(&cm[i], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + i*sizeof(heatfloor_channel_mode), sizeof(heatfloor_channel_mode));
						}
					}
				}
				requestSystime();	//корректируем время каждый час
			}
		}
		
		//dec timers on party modes, if exists
		for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
			if (cm[i].mode == HEATFLOOR_MODE_PARTY){
				if (--cm[i].timer==0){
					//load default mode from eeprom
					eeprom_read_block(&cm[i], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + i*sizeof(heatfloor_channel_mode), sizeof(heatfloor_channel_mode));
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
//	1-байт: режим
//		0 - выкл
//		1 - ручной (с указанием поддерживаемой t*)
//		2 - дневной (с указанием номера программы)
//		3 - недельный (с указанием 7 программ на каждый день)
//		4 - вечеринка (с указанием поддерживаемой t* и времени в секундах)
//		5 - дневной на сегодня (с указанием программы)
//  2-байт: битовая маска каналов, на которые распространяется команда
//  3-9 байты: необходимые доп.параметры для режимов

unsigned char heatfloor_dispatcher_set_mode(char* data, char size){
	signed char r = 0;
	if (size > 1){
		heatfloor_channel_mode* cm = (heatfloor_channel_mode*)(&channel_modes_buf[0]);
		
		//check size
		switch(data[0]){
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
				if (test_bit(data[1], i)){
					memcpy(&cm[i], &data[0], size);	//копируем все как есть
					
					//определяем режимы, при которых сохраняем в eeprom
					switch (data[0]){
						case HEATFLOOR_MODE_OFF:
						case HEATFLOOR_MODE_MANUAL:
						case HEATFLOOR_MODE_DAY:
						case HEATFLOOR_MODE_WEEK:
							eeprom_busy_wait();
							eeprom_update_block(&cm[i], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + (i*sizeof(heatfloor_channel_mode)), sizeof(heatfloor_channel_mode));
							break;
					}
					r = 1;
				}
			}
			
			if (r){
				//сообщение об изменении режима
				if (on_heatfloor_channel_modes_changed){
					(*on_heatfloor_channel_modes_changed)(cm);
				}
			}
			
		}
	}
	return r;
}


/************************************************************************/
/* Записывает программу в EEPROM										*/
/************************************************************************/
//	1-байт: номер программы (0-9)
//  2-20-байт: до 10 пар значений: час - температура

unsigned char heatfloor_dispatcher_set_program(char* data, char size){
	signed char r = 0;
	if (size > 3){
		
		char programNum = data[0];
		
		if (programNum >=0 && programNum < 10){	//номер программы
			r = 1;
			
			heatfloor_program* hp = (heatfloor_program*)(&program_buf[0]);
			hp->num_values = 0;
			
			signed char h = -1;
			for (int i=1; i<size; i+=2){
				if (data[i] <= h || data[i] > 23){	//нарушен порядок часов в программе, или указан неверный час
					r= 0;
					break;
				}else{
					h = data[i];
				}
				hp->values[hp->num_values].hour = data[i + 0];
				hp->values[hp->num_values].t    = data[i + 1];
				hp->num_values++;
			}
			
			//если программа корректная -> сохраняем в eeprom
			if (r){
				eeprom_busy_wait();
				eeprom_update_block(hp, (void *)EEPROM_ADDRESS_HEATFLOOR_PROGRAMS + (programNum * sizeof(heatfloor_program)), sizeof(heatfloor_program));
				
				heatfloor_clear_channel_programs_cache();
				
				//сообщение об изменении режима
				if (on_heatfloor_program_changed){
					(*on_heatfloor_program_changed)(programNum, hp);
				}
			}
		}
	}
	return r;
}

unsigned char heatfloor_dispatcher_command(char* data, char size){
	if (size > 1){
		switch (data[0]){
			case HEATFLOOR_MODE_OFF:
			case HEATFLOOR_MODE_MANUAL:
			case HEATFLOOR_MODE_DAY:
			case HEATFLOOR_MODE_WEEK:
			case HEATFLOOR_MODE_PARTY:
				return heatfloor_dispatcher_set_mode(data, size);
			case 0xFD:
				return heatfloor_dispatcher_set_program(&data[1], size-1);	//отдаем только программу, без первого байта 0xFD
		}
	}
	return 0;
}

heatfloor_channel_mode* heatfloor_modes_info(){
	return (heatfloor_channel_mode*)(&channel_modes_buf[0]);
}

heatfloor_program* heatfloor_program_info(unsigned char program_num){
	heatfloor_program* hp = (heatfloor_program*)(&program_buf[0]);
	
	//read from eeprom
	eeprom_read_block(hp, (void *)EEPROM_ADDRESS_HEATFLOOR_PROGRAMS + program_num*sizeof(heatfloor_program), sizeof(heatfloor_program));
	return hp;
}

void heatfloor_set_on_modes_changed(void(*f)(heatfloor_channel_mode* modes)){
	on_heatfloor_channel_modes_changed = f;
}

void heatfloor_set_on_program_changed(void(*f)(unsigned char program_num, heatfloor_program* program)){
	on_heatfloor_program_changed = f;
}

void heatfloor_dispatcher_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week))){
	eeprom_read_block(&channel_modes_buf, (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES, sizeof(channel_modes_buf));
	heatfloor_clear_channel_programs_cache();
	
	time.day_of_week = 0;	//undefined time
	on_heatfloor_dispather_request_systime = f_request_systime;
	
	//requestSystime();
}