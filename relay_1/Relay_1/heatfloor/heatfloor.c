/*
 * heatfloor.c
 *
 * Created: 12.10.2015 15:08:13
 *  Author: gargon
 */ 

#include "heatfloor.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

signed int (*on_heatfloor_sensor_temperature_request)(unsigned char channel) = 0;
char (*on_heatfloor_switch_exec)(unsigned char channel, unsigned char on_) = 0;
void (*on_heatfloor_state_message)(heatfloor_channel_infos* infos) = 0;


volatile unsigned char on;		//���/����
volatile unsigned char sensorCheckTimer;

char stateMessageBuffer[HEATFLOOR_CHANNELS_COUNT*sizeof(heatfloor_channel_info)+1];


signed char heatfloor_turnSwitch(unsigned char channel, unsigned char on_){
	signed char r = -1;
	if (on_heatfloor_switch_exec){
		r = (*on_heatfloor_switch_exec)(channel, on_);
	}
	return r;
}

heatfloor_channel_infos* heatfloor_refresh(){
	sensorCheckTimer = 0;
	
	heatfloor_channel_infos* cis = (heatfloor_channel_infos*)(&stateMessageBuffer[0]);
	cis->num = 0;
	
	if (on){
		for (unsigned char i = 0; i < HEATFLOOR_CHANNELS_COUNT; i++){
			signed int settingT = heatfloor_dispatcher_resolve_temperature_setting(i);
			if (settingT != 0){
				
				signed int sensorT = -1;
				
				if (on_heatfloor_sensor_temperature_request){
					sensorT = (*on_heatfloor_sensor_temperature_request)(i);
				}
				
				signed char solution = 0;
				
				if (settingT > 0){
					if (sensorT >= 0){
						//check range
						if (sensorT >= HEATFLOOR_MIN_TEMPERATURE_10 && sensorT <= HEATFLOOR_MAX_TEMPERATURE_10){
							if (sensorT <= settingT - HEATFLOOR_SENSOR_HYSTERESIS_TEMPERATURE_10){
								solution = 1;					//�����������
							}else if (sensorT >= settingT){
								solution = -1;					//����������
							}
								//� ������ ���� �������� �������� � ����������� ��������, ��
								//��������� ���������: ���������� ����������, ���� �� ������ �� ������� �������
								//��� ���������� ����������, ���� �� ������ �� ������ �������
								
								//��� ������� 28 � ����������� 0.3 - ����� ������������� �� 28(� ��� ������� ������� 0,3) � ����������� �� 27.7
						}else{
							solution = -3;						    //������ ��������� �������� � ������� (85*, ��������)
						}
					}else{
						solution = -2;								//������ ������ �������� � �������
					}
				}else{
					solution = -4;									//������ ��������� �������� �� ����������
				}
				
				if (solution != 0){
					heatfloor_turnSwitch(i, solution>0);
				}
				
				//��� ����������� ����� �� ���� �������
				heatfloor_channel_info* ci = &cis->channels[(cis->num)++];
				ci->num = i;
				ci->solution = solution;
				ci->sensorT = sensorT;
				ci->settingT = settingT;
				
			}
			//else ����� ��������
		}
	}
	return cis;
}

//�������� ������ � ���������� ������� ������
//response_required - ������������ ������� ������ ��� ������ ��� ������� �������� �������
void heatfloor_refresh_responsible(unsigned char response_required){
	heatfloor_channel_infos* infos = heatfloor_refresh();
	if (infos->num || response_required){	//generate an event if we have active channels only
		if (on_heatfloor_state_message){
			(*on_heatfloor_state_message)(infos);
		}
	}
}

void heatfloor_tick_second(){
	heatfloor_dispatcher_tick_second();
	
	if (++sensorCheckTimer >= HEATFLOOR_SENSOR_CHECK_TIME){
		heatfloor_refresh_responsible(0);	//�������� ����� ������ ���� ���� �������� ������
	}
}

void heatfloor_init(
		signed int (*hf_sensor_temperature_request)(unsigned char channel),
		char (*hf_control_change_request)(unsigned char channel, unsigned char on_),
		void (*hf_systime_request)(void (*hf_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week))
		){
			
	//������ ���������� ������� �� EEPROM
	on = eeprom_read_byte((void*)EEPROM_ADDRESS_HEATFLOOR);
	
	
	on_heatfloor_sensor_temperature_request = hf_sensor_temperature_request;
	on_heatfloor_switch_exec = hf_control_change_request;
	
	heatfloor_dispatcher_init(hf_systime_request);
	
	sensorCheckTimer = HEATFLOOR_SENSOR_CHECK_TIME - 1;
}

heatfloor_channel_infos* heatfloor_state_info(){
	return heatfloor_refresh();
}

heatfloor_channel_mode* heatfloor_mode_info(){
	return heatfloor_dispatcher_mode_info();
}

void heatfloor_set_on_state_message(void(*f)(heatfloor_channel_infos* infos)){
	on_heatfloor_state_message = f;
}

void heatfloor_set_on_mode_message(void(*f)(heatfloor_channel_mode* modes)){
	heatfloor_dispatcher_set_on_mode_message(f);
}

void heatfloor_on(unsigned char on_){
	if (on_ != on){
		on = on_;
		
		if (!on){
			for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
				//switch relay off
				heatfloor_turnSwitch(i, 0);
			}
		}
		
		//save channels enable in eeeprom
		eeprom_write_byte((void*)EEPROM_ADDRESS_HEATFLOOR, on); // ���������� ������
	}
	heatfloor_refresh_responsible(1);	//�������� ����� ������
}

void heatfloor_command(char* data, unsigned char size){
	if (heatfloor_dispatcher_command(data, size)){	//���� ����� �������, ����� ����������
		_delay_ms(20);	//���� �������� ��������� � ����� ������, ��� ��������� ���������� ��������, � ������� cli, sei
		heatfloor_refresh_responsible(1);
	}
}