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

void (*on_heatfloor_channel_modes_changed)(heatfloor_channel_modes* modes) = 0;
void (*on_heatfloor_program_changed)(heatfloor_channel_program* program) = 0;

heatfloor_datetime time;

char channel_modes_buf[sizeof(heatfloor_channel_modes)];
char channel_programs_buf[HEATFLOOR_CHANNELS_COUNT * sizeof(heatfloor_channel_program)];

char program_buf[sizeof(heatfloor_channel_program)];


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
		heatfloor_channel_modes* hcm = (heatfloor_channel_modes*)(&channel_modes_buf[0]);
		heatfloor_channel_program* cpc = (heatfloor_channel_program*)(&channel_programs_buf[0]);
		
		switch (hcm->channel_modes[channel].mode){
			case HEATFLOOR_MODE_OFF:
				return -1;							//отключен (режим)
			case HEATFLOOR_MODE_MANUAL:
			case HEATFLOOR_MODE_PARTY:
				return hcm->channel_modes[channel].t * 10;
			case HEATFLOOR_MODE_DAY:
			case HEATFLOOR_MODE_DAY_FOR_TODAY:
			case HEATFLOOR_MODE_WEEK:
				{
					unsigned char program_num = hcm->channel_modes[channel].p;
					if (hcm->channel_modes[channel].mode == HEATFLOOR_MODE_WEEK){
						switch (time.day_of_week){
							case 6:
								program_num = hcm->channel_modes[channel].p_sa_su[PROGRAM_SA];
								break;
							case 7:
								program_num = hcm->channel_modes[channel].p_sa_su[PROGRAM_SU];
								break;
						}
					}
				
					if (program_num < 10){
						//если в кэше нет программы - то выгружаем туда из eeprom
						if (cpc[channel].program_num != program_num){
							//сохраняем в кэш
							memcpy(&cpc[channel].program, &heatfloor_program_info(program_num)->program, sizeof(heatfloor_program));
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
	
	heatfloor_channel_modes* hcm = (heatfloor_channel_modes*)(&channel_modes_buf[0]);
	
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
						if (hcm->channel_modes[i].mode == HEATFLOOR_MODE_DAY_FOR_TODAY){
							//load default mode from eeprom
							eeprom_read_block(&hcm->channel_modes[i], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + i*sizeof(heatfloor_channel_mode), sizeof(heatfloor_channel_mode));
						}
					}
				}
				requestSystime();	//корректируем время каждый час
			}
		}
		
		//dec timers on party modes, if exists
		for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
			if (hcm->channel_modes[i].mode == HEATFLOOR_MODE_PARTY){
				if (--hcm->channel_modes[i].timer==0){
					//load default mode from eeprom
					eeprom_read_block(&hcm->channel_modes[i], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + i*sizeof(heatfloor_channel_mode), sizeof(heatfloor_channel_mode));
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
		heatfloor_channel_modes* cm = (heatfloor_channel_modes*)(&channel_modes_buf[0]);
		
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
					
					memcpy(&cm->channel_modes[i], &data[1], size-1);	//копируем все как есть без первого байта - маски каналов
					
					//определяем режимы, при которых сохраняем в eeprom
					switch (mode){
						case HEATFLOOR_MODE_OFF:
						case HEATFLOOR_MODE_MANUAL:
						case HEATFLOOR_MODE_DAY:
						case HEATFLOOR_MODE_WEEK:
							eeprom_busy_wait();
							eeprom_update_block(&cm->channel_modes[i], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + (i*sizeof(heatfloor_channel_mode)), sizeof(heatfloor_channel_mode));
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
//	1-байт: номер программы (F0-F9)
//  2-20-байт: до 10 пар значений: час - температура

unsigned char heatfloor_dispatcher_set_program(char* data, char size){
	signed char r = 0;
	if (size > 2){
		
		char programNum = data[0] & 0x0F;
		
		if (programNum >=0 && programNum < 10){	//номер программы
			r = 1;
			
			heatfloor_channel_program* hp = (heatfloor_channel_program*)(&program_buf[0]);
			hp->program_num = data[0];
			hp->program.num_values = 0;
			
			signed char h = -1;
			for (int i=1; i<size; i+=2){
				if (data[i] <= h || data[i] > 23){	//нарушен порядок часов в программе, или указан неверный час
					r= 0;
					break;
				}else{
					h = data[i];
				}
				hp->program.values[hp->program.num_values].hour = data[i + 0];
				hp->program.values[hp->program.num_values].t    = data[i + 1];
				hp->program.num_values++;
			}
			
			//если программа корректная -> сохраняем в eeprom
			if (r){
				eeprom_busy_wait();
				eeprom_update_block(&hp->program, (void *)EEPROM_ADDRESS_HEATFLOOR_PROGRAMS + (programNum * sizeof(heatfloor_program)), sizeof(heatfloor_program));
				
				heatfloor_clear_channel_programs_cache();
				
				//сообщение об изменении программы
				if (on_heatfloor_program_changed){
					(*on_heatfloor_program_changed)(hp);
				}
			}
		}
	}
	return r;
}

unsigned char heatfloor_dispatcher_command(char* data, char size){
	if (size > 1){
		switch (data[0]){
			case 0xFE:	//установка режимов
				return heatfloor_dispatcher_set_mode(&data[1], size-1);
			case 0xF0:
			case 0xF1:
			case 0xF2:
			case 0xF3:
			case 0xF4:
			case 0xF5:
			case 0xF6:
			case 0xF7:
			case 0xF8:
			case 0xF9:	//установка программ
				return heatfloor_dispatcher_set_program(data, size);	
		}
	}
	return 0;
}

heatfloor_channel_modes* heatfloor_modes_info(){
	return (heatfloor_channel_modes*)(&channel_modes_buf[0]);
}

//program_num: 0-9 or F0-F9
heatfloor_channel_program* heatfloor_program_info(unsigned char program_num){
	heatfloor_channel_program* hp = (heatfloor_channel_program*)(&program_buf[0]);
	hp->program_num = program_num | 0xF0;
	
	//read from eeprom
	eeprom_read_block(&hp->program, (void *)EEPROM_ADDRESS_HEATFLOOR_PROGRAMS + (program_num & 0x0F)*sizeof(heatfloor_program), sizeof(heatfloor_program));
	return hp;
}

void heatfloor_set_on_modes_changed(void(*f)(heatfloor_channel_modes* modes)){
	on_heatfloor_channel_modes_changed = f;
}

void heatfloor_set_on_program_changed(void(*f)(heatfloor_channel_program* program)){
	on_heatfloor_program_changed = f;
}

void heatfloor_dispatcher_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week))){
	heatfloor_channel_modes* hcm = (heatfloor_channel_modes*)(&channel_modes_buf[0]);
	hcm->type = 0xFE;
	
	eeprom_read_block(&hcm->channel_modes[0], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES, HEATFLOOR_CHANNELS_COUNT * sizeof(heatfloor_channel_mode));
	heatfloor_clear_channel_programs_cache();
	
	time.day_of_week = 0;	//undefined time
	on_heatfloor_dispather_request_systime = f_request_systime;
	
	//requestSystime();
}