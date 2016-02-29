/*
 * heatfloor_timer.h
 *
 * Created: 12.10.2015 17:57:28
 *  Author: gargon
 */ 


#ifndef HEATFLOOR_TIMER_H_
#define HEATFLOOR_TIMER_H_

#include "heatfloor_config.h"


#define HEATFLOOR_MODE_OFF 0
#define HEATFLOOR_MODE_MANUAL 1
#define HEATFLOOR_MODE_DAY 2
#define HEATFLOOR_MODE_WEEK 3
#define HEATFLOOR_MODE_PARTY 4


typedef struct
{
	unsigned char day_of_week;
	
	unsigned char seconds;
	unsigned char minutes;
	unsigned char hours;
} heatfloor_datetime;


typedef struct
{
	unsigned char mode;
	unsigned char params[7];
	
} heatfloor_channel_mode;


typedef struct 
{
	unsigned char hour;
	unsigned char t;
	
} heatfloor_program_value;


typedef struct
{
	unsigned char num_values;
	heatfloor_program_value values[10];
	
} heatfloor_program;


/*для кэширования текущей программы*/
typedef struct
{
	signed char program_num;
	heatfloor_program program;
	
} heatfloor_channel_program;


void heatfloor_dispatcher_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)));

void heatfloor_dispatcher_tick_second();

signed int heatfloor_dispatcher_resolve_temperature_setting(unsigned char channel);

unsigned char heatfloor_dispatcher_command(char* data, char size);

heatfloor_datetime* heatfloor_systime();

heatfloor_channel_mode* heatfloor_modes_info();
heatfloor_program* heatfloor_program_info(unsigned char program_num);


void heatfloor_set_on_modes_changed(void(*f)(heatfloor_channel_mode* modes));
void heatfloor_set_on_program_changed(void(*f)(unsigned char program_num, heatfloor_program* program));


#define EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES EEPROM_ADDRESS_HEATFLOOR + 1
#define EEPROM_ADDRESS_HEATFLOOR_PROGRAMS EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + 8*sizeof(heatfloor_channel_mode)

#endif /* HEATFLOOR_TIMER_H_ */