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


void heatfloor_dispatcher_init();

void setRealTime(signed char seconds, signed char minutes, signed char hours, signed char day_of_week);

signed int resolveTemperatureSetting(unsigned char channel);


#endif /* HEATFLOOR_TIMER_H_ */