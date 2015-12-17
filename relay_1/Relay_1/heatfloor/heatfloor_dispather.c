/*
 * heatfloor_settings.c
 *
 * Created: 12.10.2015 17:56:40
 *  Author: gargon
 */ 

#include "heatfloor.h"

void (*on_heatfloor_dispather_request_systime)() = 0;



volatile systime time;

signed int heatfloor_dispatcher_resolve_temperature_setting(unsigned char channel){
	//если не прочитан eeprom или не установлено текущее время -> возвращать 0
	return 28 * 10;
}

void setMode(){
	
}


void heatfloor_dispatcher_set_systime(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week){
	time.seconds = seconds;
	time.minutes = minutes;
	time.hours = hours;
	time.day_of_week = day_of_week;
}

void inc_systime(){
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
}

void heatfloor_dispatcher_init(void(*f_request_systime)()){
	//init timer, read eeprom (программы, текущий режим (дневной, недельный))
	
	time.day_of_week = 0;	//undefined time
	
	
	//apply and request for current time
	on_heatfloor_dispather_request_systime = f_request_systime;
	
	if (on_heatfloor_dispather_request_systime){
		(*on_heatfloor_dispather_request_systime)();
	}
}