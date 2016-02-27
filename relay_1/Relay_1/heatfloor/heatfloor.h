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
/*  ������������� ������ ���������� ������ �����												*/
/*		hf_sensor_temperature_request - ������� ������� ������� ������� ����������� �� ������   */
/*		hf_control_change_request - ������� ������� ���������� ���� ��� ������					*/
/*		hf_systime_request - ������� ������� ������� �������� ������� � � ��� �������� -		*/
/*			���� �������� ������� ������ �� ������												*/
/************************************************************************************************/
void heatfloor_init(
	signed int (*hf_sensor_temperature_request)(unsigned char channel),
	char (*hf_control_change_request)(unsigned char channel, unsigned char on_),
	void (*hf_systime_request)(void (*hf_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week))
);

/*************************************************************************/
/*  ��������� �������													 */
/*************************************************************************/
void heatfloor_enable(unsigned char channel, unsigned char enable_);

/************************************************************************/
/*  �������� ��� ����������� ������� ���������� �������, ����������		*/
/*	������� ��������													*/
/************************************************************************/
void heatfloor_tick_second();

/************************************************************************/
/*  ���������� ������� ��������� ������ ������� ���� �� �������� �������*/
/************************************************************************/
heatfloor_channel_infos* heatfloor_state_info();

/************************************************************************/
/*  ���������� ������� ������ ������ ������� ���� �� ���� �������    */
/************************************************************************/
heatfloor_channel_mode* heatfloor_mode_info();

/***********************************************************************************************************/
/*��������� �������, ������� ���������� ��� ����������� �������� ����������							       */
/*��������� ������� ������� ���� (��� �������)										    					*/
/***********************************************************************************************************/
void heatfloor_set_on_state_message(void (*f)(heatfloor_channel_infos* infos));


/***********************************************************************************************************/
/*��������� �������, ������� ���������� ��� ����������� ���������										   */
/*������� ������ ������� ����														    				   */
/***********************************************************************************************************/
void heatfloor_set_on_mode_message(void(*f)(heatfloor_channel_mode* modes));

void heatfloor_on(unsigned char on_);

void heatfloor_command(char* data, unsigned char size);

#endif /* HEATFLOOR_H_ */