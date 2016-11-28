/*
 * timer.h
 *
 * Created: 28.11.2016 17:06:46
 *  Author: gargon
 */ 


#ifndef TIMER_H_
#define TIMER_H_

typedef struct
{
	unsigned char day_of_week;
	
	unsigned char seconds;
	unsigned char minutes;
	unsigned char hours;
} datetime;

void timer_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)));

void timer_tick_second();

datetime* timer_systime();

#endif /* TIMER_H_ */