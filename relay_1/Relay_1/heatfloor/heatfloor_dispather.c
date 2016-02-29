/*
 * heatfloor_settings.c
 *
 * Created: 12.10.2015 17:56:40
 *  Author: gargon
 */ 

#include "heatfloor.h"

#include <avr/eeprom.h>
#include <util/delay.h>
#include <string.h>

void (*on_heatfloor_dispather_request_systime)(void (*hf_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)) = 0;

void (*on_heatfloor_channel_modes_changed)(heatfloor_channel_mode* modes) = 0;
void (*on_heatfloor_program_changed)(unsigned char program_num, heatfloor_program* program) = 0;

char channel_modes_buf[HEATFLOOR_CHANNELS_COUNT * sizeof(heatfloor_channel_mode)];
char channel_programs_buf[HEATFLOOR_CHANNELS_COUNT * sizeof(heatfloor_channel_program)];

//global tmp program buf
char program_buf[sizeof(heatfloor_program)];

heatfloor_datetime time;
unsigned char lastCorrectionHour;	//час, в котором проводилась последняя корректировка времени


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
	
	lastCorrectionHour = hours;
}

void requestSystime(){
	if (on_heatfloor_dispather_request_systime){
		(*on_heatfloor_dispather_request_systime)( heatfloor_dispatcher_set_systime );
	}
}




signed int heatfloor_dispatcher_resolve_temperature_setting(unsigned char channel){
	
	if (is_time_valid()){
		
		if (time.hours != lastCorrectionHour){	//корректируем каждый час
			
			lastCorrectionHour = 0xFF;			//так, спустя сутки и более, коррекция в этом же часу не будет пропущена
			
			requestSystime();
			_delay_ms(50);						//ждем установку времени, режется отключением прерываний при замере температуры 
		}
		
		heatfloor_channel_mode* cm = (heatfloor_channel_mode*)(&channel_modes_buf[0]);
		heatfloor_channel_program* cpc = (heatfloor_channel_program*)(&channel_programs_buf[0]);
		
		switch (cm[channel].mode){
			case HEATFLOOR_MODE_OFF:
				return 0;	//отключен
			case HEATFLOOR_MODE_MANUAL:
				return cm[channel].params[0] * 10;
			
			case HEATFLOOR_MODE_DAY:
			case HEATFLOOR_MODE_WEEK:
			{
				unsigned char program_num;
				if (cm[channel].mode == HEATFLOOR_MODE_DAY){
					program_num = cm[channel].params[0];
				}else{
					program_num = cm[channel].params[time.day_of_week - 1];
				}
				
				if (program_num >=0 && program_num < 10){
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
				return -1;
			}
			
			case HEATFLOOR_MODE_PARTY:
				break;
			default:
				return -1;	//неизвестный режим
		}
	}
	
	return -1;	//диспетчер не проинициализирован
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
//		4 - вечеринка (фиксированные 30* в течение 3 часов)
//  2-байт: битовая маска каналов, на которые распространяется команда
//  3-9 байты: необходимые доп.параметры для режимов

unsigned char heatfloor_dispatcher_set_mode(char* data, char size){
	signed char r = 0;
	if (size > 1){
		heatfloor_channel_mode* cm = (heatfloor_channel_mode*)(&channel_modes_buf[0]);
		
		switch (data[0]){
			case HEATFLOOR_MODE_OFF:	//выкл (режим) канал
			case HEATFLOOR_MODE_PARTY:	//вечеринка
			if (size == 2){
				for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
					if (test_bit(data[1],i)){
						cm[i].mode = data[0];
						if (data[0] == HEATFLOOR_MODE_OFF){
							r = 1;
						}else{
							r = -1;	//не сохраняем в eeprom, если вечеринка
						}
					}
				}
			}
			break;
			case HEATFLOOR_MODE_MANUAL:	//ручной режим, с установленной t*
			case HEATFLOOR_MODE_DAY:	//дневной режим с установленной программой
			if (size == 3){
				for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
					if (test_bit(data[1],i)){
						cm[i].mode = data[0];
						cm[i].params[0] = data[2];
						r = 1;
					}
				}
			}
			break;
			case HEATFLOOR_MODE_WEEK: //недельный режим, с указанием 7-ми программ
			if (size == 9){
				for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
					if (test_bit(data[1],i)){
						cm[i].mode = data[0];
						for (int j=0; j<7; j++){
							cm[i].params[j] = data[2+j];
						}
						r = 1;
					}
				}
			}
			break;
		}
		
		if (r != 0){
			if (r > 0){	//сохраняем в eeprom
				for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
					if (test_bit(data[1],i)){
						eeprom_busy_wait();
						eeprom_update_block(&cm[i], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + (i*sizeof(heatfloor_channel_mode)), sizeof(heatfloor_channel_mode));
					}
				}
			}
			
			//сообщение об изменении режима
			if (on_heatfloor_channel_modes_changed){
				(*on_heatfloor_channel_modes_changed)(cm);
			}
			return 1;
		}
	}
	return 0;
}


/************************************************************************/
/* Записывает программу в EEPROM										*/
/************************************************************************/
//	1-байт: номер программы (0-9)
//  2-20-байт: до 10 пар значений: час - температура

unsigned char heatfloor_dispatcher_set_program(char* data, char size){
	signed char r = 0;
	if (size > 3){
		if (data[1] >= 0 && data[1] < 10){	//номер программы
			heatfloor_program* hp = (heatfloor_program*)(&program_buf[0]);
			
			hp->num_values = 0;
			signed char h = -1;
			r = 1;
			for (int i=2; i<size; i+=2){
				if (data[i] <= h && data[i] > 23){	//нарушен порядок часов в программе, или указан неверный час
					r= 0;
					break;
				}else{
					h = data[i];
				}
				hp->values[hp->num_values].hour = data[i + 0];
				hp->values[hp->num_values].t = data[i + 1];
				hp->num_values++;
			}
			
			//если программа корректная -> сохраняем в eeprom
			if (r){
				eeprom_busy_wait();
				eeprom_update_block(hp, (void *)EEPROM_ADDRESS_HEATFLOOR_PROGRAMS + (data[1]*sizeof(heatfloor_program)), sizeof(heatfloor_program));
				
				heatfloor_clear_channel_programs_cache();
				
				//сообщение об изменении режима
				if (on_heatfloor_program_changed){
					(*on_heatfloor_program_changed)(data[1], hp);
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
				return heatfloor_dispatcher_set_program(data, size);
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
	
	//apply and request for current time
	on_heatfloor_dispather_request_systime = f_request_systime;
	requestSystime();
}