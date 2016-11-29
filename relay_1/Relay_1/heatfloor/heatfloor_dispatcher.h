/*
 * heatfloor_timer.h
 *
 * Created: 12.10.2015 17:57:28
 *  Author: gargon
 */ 


#ifndef HEATFLOOR_TIMER_H_
#define HEATFLOOR_TIMER_H_

#include "heatfloor_config.h"

#define NUM_SECONDS_TO_CORRECTION 3600

#define HEATFLOOR_MODE_OFF 0
#define HEATFLOOR_MODE_MANUAL 1
#define HEATFLOOR_MODE_DAY 2
#define HEATFLOOR_MODE_WEEK 3
#define HEATFLOOR_MODE_PARTY 4
#define HEATFLOOR_MODE_DAY_FOR_TODAY 5

typedef struct
{
	unsigned char day_of_week;
	
	unsigned char seconds;
	unsigned char minutes;
	unsigned char hours;
} heatfloor_datetime;


#define HEATFLOOR_PROGRAM_SA 0
#define HEATFLOOR_PROGRAM_SU 1

typedef struct{
	unsigned char mode;
	union{
		unsigned char t;
		unsigned char p;	//program default
	};
	union{
		unsigned char p_sa_su[2];	//programs for saturday & sunday
		unsigned int timer;			//timer for party
	};
	
} heatfloor_channel_mode;

typedef struct{
	unsigned char type;		//const 0xFE;
	heatfloor_channel_mode channels[HEATFLOOR_CHANNELS_COUNT];
} heatfloor_channel_modes;


//program descriptors

typedef struct{
	unsigned char hour;
	unsigned char t;
} heatfloor_program_value;

typedef struct{
	unsigned char num_values;
	heatfloor_program_value values[HEATFLOOR_PROGRAMS_COUNT];
} heatfloor_program_values;

typedef struct{
	unsigned char program_num;
	heatfloor_program_values program;
} heatfloor_program;


void heatfloor_dispatcher_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)));

void heatfloor_dispatcher_tick_second();

signed int heatfloor_dispatcher_resolve_temperature_setting(unsigned char channel);

unsigned char heatfloor_dispatcher_command(char* data, char size);

heatfloor_datetime* heatfloor_systime();

heatfloor_channel_modes* heatfloor_modes_info();

heatfloor_program* heatfloor_program_info(unsigned char program_num);

void heatfloor_set_on_modes_changed(void(*f)(heatfloor_channel_modes* modes_));

void heatfloor_set_on_program_changed(void(*f)(heatfloor_program* program));

#define HEATFLOOR_MESSAGE_MODES_PREAMBULE 0xFE
#define HEATFLOOR_MESSAGE_PROGRAM_PREAMBULE 0xF0

#define EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES EEPROM_ADDRESS_HEATFLOOR + 1
#define EEPROM_ADDRESS_HEATFLOOR_PROGRAMS EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + 8*sizeof(heatfloor_channel_mode)

#endif /* HEATFLOOR_TIMER_H_ */