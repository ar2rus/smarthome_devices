/*
 * timer.c
 *
 * Created: 28.11.2016 17:06:31
 *  Author: gargon
 */ 

#include "timer.h"

void (*on_timer_request_systime)(void (*timer_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)) = 0;

datetime time;

task* tasks[MAX_NUM_TASKS];

unsigned char num_tasks = 0;


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
			switch (tasks[i]->type){
				case TASK_TYPE_COUNTDOWN:{
					countdown_task* t = (countdown_task*)tasks[i];
					if (!(--t->seconds)){
						
						tasks[i]->type = TASK_TYPE_NONE;
						tasks[i]->f_task_callback();
						delete_cnt++;
					}
				}
				break;
				case TASK_TYPE_SCHEDULED:{
					scheduled_task* t = (scheduled_task*)tasks[i];
					if (t->day_of_week == time.day_of_week && t->hours == time.hours && t->minutes == time.minutes){
						
						tasks[i]->type = TASK_TYPE_NONE;
						tasks[i]->f_task_callback();
						delete_cnt++;
					}
				}
				break;
			}
		}
		
		num_tasks -= delete_cnt;
		
		
		if (++correction_ticks >= NUM_SECONDS_TO_CORRECTION){
			correction_ticks = 0;
			
			requestSystime();	//корректируем время каждые NUM_SECONDS_TO_CORRECTION секунд
		}
		
	}else{
		requestSystime();
	}
}

signed char timer_add_countdown_task(unsigned int seconds, void (*f_task_callback)()){
	if (num_tasks < MAX_NUM_TASKS){
		
		num_tasks++;
		
		countdown_task t;
		t.t.type = TASK_TYPE_COUNTDOWN;
		t.t.f_task_callback = f_task_callback;
		
		t.seconds = seconds;
		
		for (int i=0; i<MAX_NUM_TASKS; i++){
			if (tasks[i]->type == TASK_TYPE_NONE){
				tasks[i] = (task*)&t;
				return i;
			}
		}
	}
	return -1;
}

signed char timer_add_scheduled_task(unsigned char day_of_week, unsigned char hours, unsigned char minutes, void (*f_task_callback)()){
	if (num_tasks < MAX_NUM_TASKS){
		
		num_tasks++;
		
		scheduled_task t;
		t.t.type = TASK_TYPE_SCHEDULED;
		t.t.f_task_callback = f_task_callback;
		
		t.day_of_week = day_of_week;
		t.hours = hours;
		t.minutes = minutes;
		
		for (int i=0; i<MAX_NUM_TASKS; i++){
			if (tasks[i]->type == TASK_TYPE_NONE){
				tasks[i] = (task*)&t;
				return i;
			}
		}
	}
	return -1;
}

void timer_remove_task(unsigned char index){
	if (index < MAX_NUM_TASKS){
		tasks[index]->type = TASK_TYPE_NONE;
	}
}

task* timer_get_task(unsigned char index){
	if (index < MAX_NUM_TASKS){
		return tasks[index];
	}
	return 0;
}