/*
 * heatfloor_timer.h
 *
 * Created: 12.10.2015 17:57:28
 *  Author: gargon
 */ 


#ifndef HEATFLOOR_TIMER_H_
#define HEATFLOOR_TIMER_H_

#include "heatfloor_config.h"




#define EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES EEPROM_ADDRESS_HEATFLOOR + 1
#define EEPROM_ADDRESS_HEATFLOOR_PROGRAMS EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + 8*8



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


void heatfloor_dispatcher_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)));

void heatfloor_dispatcher_tick_second();

signed int heatfloor_dispatcher_resolve_temperature_setting(unsigned char channel);

unsigned char heatfloor_dispatcher_command(char* data, char size);

heatfloor_channel_mode* heatfloor_dispatcher_mode_info();

void heatfloor_dispatcher_set_on_mode_message(void(*f)(heatfloor_channel_mode* modes));

#endif /* HEATFLOOR_TIMER_H_ */