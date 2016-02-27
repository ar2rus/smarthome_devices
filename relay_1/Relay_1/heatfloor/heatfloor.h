/*
 * heatfloor.h
 *
 * Created: 12.10.2015 15:08:27
 *  Author: gargon
 */ 


#ifndef HEATFLOOR_H_
#define HEATFLOOR_H_

#include "utils/bits.h"

#include "heatfloor_dispatcher.h"
#include "heatfloor_config.h"


#define HEATFLOOR_MIN_TEMPERATURE 10
#define HEATFLOOR_MIN_TEMPERATURE_10 HEATFLOOR_MIN_TEMPERATURE*10
#define HEATFLOOR_MAX_TEMPERATURE 45
#define HEATFLOOR_MAX_TEMPERATURE_10 HEATFLOOR_MAX_TEMPERATURE*10

#define HEATFLOOR_SENSOR_HYSTERESIS_TEMPERATURE_10 10*HEATFLOOR_SENSOR_HYSTERESIS_TEMPERATURE

typedef struct
{
	unsigned char num;		//Number of channel 
	signed char solution;	//see heatfloor_refresh()
	signed int sensorT;		// (t*10)
	signed int settingT;	// (t*10)
} heatfloor_channel_info;

typedef struct
{	
	unsigned char num;											//The number of active channels
	heatfloor_channel_info channels[HEATFLOOR_CHANNELS_COUNT];	//Channel descriptors
} heatfloor_channel_infos;


/************************************************************************************************/
/*  Инициализация модуля управления теплым полом												*/
/*		hf_sensor_temperature_request - коллбэк функция запроса текущей температуры по каналу   */
/*		hf_control_change_request - коллбэк функция управления реле для канала					*/
/*		hf_systime_request - коллбэк функция запроса текущего времени и в ней параметр -		*/
/*			тоже коллбэек функция ответа на запрос												*/
/************************************************************************************************/
void heatfloor_init(
	signed int (*hf_sensor_temperature_request)(unsigned char channel),
	char (*hf_control_change_request)(unsigned char channel, unsigned char on_),
	void (*hf_systime_request)(void (*hf_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week))
);

/*************************************************************************/
/*  Активация каналов													 */
/*************************************************************************/
void heatfloor_enable(unsigned char channel, unsigned char enable_);

/************************************************************************/
/*  Вызывать при прохождении секунды системного времени, измеренной		*/
/*	внешним таймером													*/
/************************************************************************/
void heatfloor_tick_second();

/************************************************************************/
/*  Возвращает текущие параметры работы теплого пола по активным каналам*/
/************************************************************************/
heatfloor_channel_infos* heatfloor_state_info();

/************************************************************************/
/*  Возвращает текущие режимы работы теплого пола по всем каналам    */
/************************************************************************/
heatfloor_channel_mode* heatfloor_mode_info();

/***********************************************************************************************************/
/*Установка функции, которая вызывается для определения моментов обновления							       */
/*состояния каналов теплого пола (для отладки)										    					*/
/***********************************************************************************************************/
void heatfloor_set_on_state_message(void (*f)(heatfloor_channel_infos* infos));


/***********************************************************************************************************/
/*Установка функции, которая вызывается для определения изменения										   */
/*режимов работы теплого пола														    				   */
/***********************************************************************************************************/
void heatfloor_set_on_mode_message(void(*f)(heatfloor_channel_mode* modes));

void heatfloor_on(unsigned char on_);

void heatfloor_command(char* data, unsigned char size);

#endif /* HEATFLOOR_H_ */