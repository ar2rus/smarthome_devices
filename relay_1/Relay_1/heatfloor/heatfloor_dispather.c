/*
 * heatfloor_settings.c
 *
 * Created: 12.10.2015 17:56:40
 *  Author: gargon
 */ 

#include "heatfloor.h"

volatile systime time;

signed int resolveTemperatureSetting(unsigned char channel){
	//если не прочитан eeprom или не установлено текущее время -> возвращать 0
	return (27+channel) * 10;
}

void heatfloor_dispatcher_init(){
	//init timer, read eeprom (программы, текущий режим (дневной, недельный))
	
	time.day_of_week = 0;	//undefined time
}

void setMode(){
	
}


void setRealTime(signed char seconds, signed char minutes, signed char hours, signed char day_of_week){
	time.seconds = seconds;
	time.minutes = minutes;
	time.hours = hours;
	time.day_of_week = day_of_week;
}

void incTime(){
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