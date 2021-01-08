/*
 * timer.c
 *
 * Created: 28.11.2016 17:06:31
 *  Author: gargon
 */ 

#include "timer.h"

void (*on_timer_request_systime)(void (*timer_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)) = 0;

datetime time;

unsigned char num_tasks = 0;
task tasks[MAX_NUM_TASKS];


unsigned char is_time_valid(){
	return time.day_of_week;
}

void timer_set_systime(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week){
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
		(*on_timer_request_systime)( timer_set_systime );
	}
}

void timer_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week))){
	time.day_of_week = 0;	//undefined time
	on_timer_request_systime = f_request_systime;
	
	for (int i=0; i<MAX_NUM_TASKS; i++){
		tasks[i].type = TASK_TYPE_NONE;
	}
}

unsigned int correction_ticks = 0;

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
			}
		}

		//do each seconds actions here		
		
		unsigned char delete_cnt = 0;
		for (int i=0; i<MAX_NUM_TASKS; i++){
			switch (tasks[i].type){
				case TASK_TYPE_COUNTDOWN:{
					if (!(--tasks[i].seconds)){
						
						tasks[i].type = TASK_TYPE_NONE;
						if (tasks[i].f_task_callback){
							(*tasks[i].f_task_callback)();
						}
						delete_cnt++;
					}
				}
				break;
				case TASK_TYPE_SCHEDULED:{
					if (tasks[i].day_of_week == time.day_of_week && tasks[i].hours == time.hours && tasks[i].minutes == time.minutes){
						
						tasks[i].type = TASK_TYPE_NONE;
						if (tasks[i].f_task_callback){
							(*tasks[i].f_task_callback)();
						}
						
						delete_cnt++;
					}
				}
				break;
			}
		}
		
		num_tasks -= delete_cnt;
		
		
		if (++correction_ticks >= NUM_SECONDS_TO_CORRECTION){
			correction_ticks = 0;
			
			requestSystime();	//корректируем текущее время каждые NUM_SECONDS_TO_CORRECTION секунд
		}
		
	}else{
		requestSystime();
	}
}

signed char timer_add_countdown_task(unsigned int seconds, void (*f_task_callback)()){
	if (num_tasks < MAX_NUM_TASKS){
		
		num_tasks++;
		
		
		for (int i=0; i<MAX_NUM_TASKS; i++){
			if (tasks[i].type == TASK_TYPE_NONE){
				
				tasks[i].type = TASK_TYPE_COUNTDOWN;
				tasks[i].f_task_callback = f_task_callback;
		
				tasks[i].seconds = seconds;
				
				return i;
			}
		}
	}
	return -1;
}

signed char timer_add_scheduled_task(unsigned char day_of_week, unsigned char hours, unsigned char minutes, void (*f_task_callback)()){
	if (num_tasks < MAX_NUM_TASKS){
		
		num_tasks++;
		
		for (int i=0; i<MAX_NUM_TASKS; i++){
			if (tasks[i].type == TASK_TYPE_NONE){
				
				tasks[i].type = TASK_TYPE_SCHEDULED;
				tasks[i].f_task_callback = f_task_callback;
				
				tasks[i].day_of_week = day_of_week;
				tasks[i].hours = hours;
				tasks[i].minutes = minutes;
				
				return i;
			}
		}
	}
	return -1;
}

void timer_remove_task(unsigned char index){
	if (index < MAX_NUM_TASKS){
		if (tasks[index].type != TASK_TYPE_NONE){
			tasks[index].type = TASK_TYPE_NONE;
			num_tasks--;
		}
	}
}

task* timer_get_task(unsigned char index){
	if (index < MAX_NUM_TASKS){
		return &tasks[index];
	}
	return 0;
}