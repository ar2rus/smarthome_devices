/*
 * timer.c
 *
 * Created: 28.11.2016 17:06:31
 *  Author: gargon
 */ 

#include "timer.h"

void (*on_timer_request_systime)(void (*timer_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)) = 0;

datetime time;


unsigned char is_time_valid(){
	return time.day_of_week;
}

void timer_dispatcher_set_systime(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week){
	time.seconds = seconds;
	time.minutes = minutes;
	time.hours = hours;
	time.day_of_week = day_of_week;
}

datetime* timer_systime(){
	return &time;
}

void requestSystime(){
	if (on_timer_request_systime){
		(*on_timer_request_systime)( timer_dispatcher_set_systime );
	}
}


void timer_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week))){
	time.day_of_week = 0;	//undefined time
	on_timer_request_systime = f_request_systime;
	
	//requestSystime();
}

void timer_tick_second(){
	
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
				requestSystime();	//корректируем время каждый час
			}
		}


		//do each seconds actions here		
		
	}else{
		requestSystime();
	}
}