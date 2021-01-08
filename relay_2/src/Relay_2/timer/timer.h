/*
 * timer.h
 *
 * Created: 28.11.2016 17:06:46
 *  Author: gargon
 */ 


#ifndef TIMER_H_
#define TIMER_H_

#define NUM_SECONDS_TO_CORRECTION 3600

#define MAX_NUM_TASKS 2

typedef struct
{
	unsigned char day_of_week;
	
	unsigned char seconds;
	unsigned char minutes;
	unsigned char hours;
} datetime;


#define TASK_TYPE_NONE 0
#define TASK_TYPE_COUNTDOWN 1
#define TASK_TYPE_SCHEDULED 2

typedef struct
{
	unsigned char type;
	void (*f_task_callback)();
	union{
		unsigned int seconds;
		struct{
				signed char day_of_week;
				signed char hours;
				signed char minutes;
			};
		};
} task;

void timer_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)));

void timer_tick_second();

datetime* timer_systime();

signed char timer_add_countdown_task(unsigned int seconds, void (*f_task_callback)());

signed char timer_add_scheduled_task(unsigned char day_of_week, unsigned char hours, unsigned char minutes, void (*f_task_callback)());

void timer_remove_task(unsigned char index);

task* timer_get_task(unsigned char index);


#endif /* TIMER_H_ */