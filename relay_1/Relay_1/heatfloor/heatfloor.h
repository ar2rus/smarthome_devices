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


//������������� ������
void heatfloor_init();

// ��������� ���������� ������ ������
void heatfloor_enable(unsigned char channel, unsigned char enable_);

//��������� ���������� (sensor, setting, solution, ...) �� ��������� ���� �������. 
//���������� ���� �� ������� �� �������� �������
heatfloor_channel_infos* heatfloor_refresh();

//���������� ������� ������� ����������� � ������� ��� ������ (t*10)
void heatfloor_set_on_sensor_temperature_request(signed int (*f)(unsigned char channel));

//���������� ������� ������� ������� ��� ������ (t*10)
void heatfloor_set_on_setting_temperature_request(signed int (*f)(unsigned char channel));

//���������� ������� ���������� �������������� ����������� (����) ��� ������
void heatfloor_set_on_switch_exec(char (*f)(unsigned char channel, unsigned char on_));

//���������� ������� ������ ��������� ������ (�� ���� �������� �������)
void heatfloor_set_on_state_message(void(*f)(heatfloor_channel_infos* infos));

#endif /* HEATFLOOR_H_ */