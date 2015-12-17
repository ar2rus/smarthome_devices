/*
 * heatfloor_timer.h
 *
 * Created: 12.10.2015 17:57:28
 *  Author: gargon
 */ 


#ifndef HEATFLOOR_TIMER_H_
#define HEATFLOOR_TIMER_H_


typedef struct
{
	unsigned char day_of_week;
	
	unsigned char seconds;
	unsigned char minutes;
	unsigned char hours;
} systime;


void heatfloor_dispatcher_init(void(*f_request_systime)());

signed int heatfloor_dispatcher_resolve_temperature_setting(unsigned char channel);

//функция установки текущего времени
void heatfloor_dispatcher_set_systime(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week);

#endif /* HEATFLOOR_TIMER_H_ */