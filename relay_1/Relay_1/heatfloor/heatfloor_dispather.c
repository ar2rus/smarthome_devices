/*
 * heatfloor_settings.c
 *
 * Created: 12.10.2015 17:56:40
 *  Author: gargon
 */ 

#include "heatfloor.h"

#include <avr/eeprom.h>

void (*on_heatfloor_dispather_request_systime)(void (*hf_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week)) = 0;
void (*on_heatfloor_mode_message)(heatfloor_channel_mode* modes) = 0;

char channel_modes_buf[HEATFLOOR_CHANNELS_COUNT * sizeof(heatfloor_channel_mode)];

//������� ������, � ������� ��������� ������������� �������
volatile unsigned int correctTimeCounter = 0;
volatile heatfloor_datetime time;

unsigned char isTimeValid(){
	return time.day_of_week;
}

void heatfloor_dispatcher_set_systime(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week){
	time.seconds = seconds;
	time.minutes = minutes;
	time.hours = hours;
	time.day_of_week = day_of_week;
	
	correctTimeCounter = 0;
}

void requestSystime(){
	if (on_heatfloor_dispather_request_systime){
		(*on_heatfloor_dispather_request_systime)( heatfloor_dispatcher_set_systime );
	}
}


signed int heatfloor_dispatcher_resolve_temperature_setting(unsigned char channel){
	//���� �� �������� eeprom ��� �� ����������� ������� ����� -> ���������� 0
	//return 28 * 10;
	
	if (isTimeValid()){
		
		if (++correctTimeCounter >= 3600){	//������������ ������ ���
			requestSystime();
		}
		
		heatfloor_channel_mode* cm = (heatfloor_channel_mode*)(&channel_modes_buf[0]);
		switch (cm[channel].mode){
			case 0:
				return 0;	//��������
			case 1:
				return cm[channel].params[0] * 10;
			case 2:
				break;
			case 3:
				break;
			case 4:
				break;
			default:
				return -1;	//����������� �����
		}
	}
	
	return -1;	//��������� �� ������������������
}

void heatfloor_dispatcher_tick_second(){
	if (isTimeValid()){
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
	}else{
		requestSystime();
	}
}


//	1-����: �����
//		0 - ����
//		1 - ������ (� ��������� �������������� t*)
//		2 - ������� (� ��������� ������ ���������)
//		3 - ��������� (� ��������� 7 �������� �� ������ ����)
//		4 - ��������� (������������� 30* � ������� 3 �����)
//  2-����: ������� ����� �������, �� ������� ���������������� �������
//  3-9 �����: ����������� ���.��������� ��� �������

unsigned char heatfloor_dispatcher_command(char* data, char size){
	signed char r = 0;
	if (size > 1){
		heatfloor_channel_mode* cm = (heatfloor_channel_mode*)(&channel_modes_buf[0]);
		
		switch (data[0]){
			case 0:				//���� (�����) ����� 
				if (size == 2){
					for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
						if (test_bit(data[1],i)){
							cm[i].mode = data[0];
							r = 1;
						}
					}
				}
				break;
			case 1:				//������ �����, � ������������� t*
				if (size == 3){
					for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
						if (test_bit(data[1],i)){
							cm[i].mode = data[0];
							cm[i].params[0] = data[2];
							r = 1;
						}
					}
				}
				break;
			case 2:				//������� ����� � ������������� ����������
				if (size == 3){
					for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
						if (test_bit(data[1],i)){
							cm[i].mode = data[0];
							cm[i].params[0] = data[2];
							r = 1;
						}
					}
				}
				break;
			case 3:				//��������� �����, � ��������� 7-�� ��������
				if (size == 9){
					for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
						if (test_bit(data[1],i)){
							cm[i].mode = data[0];
							for (int j=0; j<7; j++){
								cm[i].params[j] = data[2+j];
							}
							r = 1;
						}
					}
				}
				break;
			case 4:			//���������
				if (size == 2){
					for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
						if (test_bit(data[1],i)){
							cm[i].mode = data[0];
							r = -1;	//�� ��������� � eeprom
						}
					}
				}
				break;
		}
	
		if (r!=0){
			if (r > 0){
				for (int i=0; i<HEATFLOOR_CHANNELS_COUNT; i++){
					if (test_bit(data[1],i)){
						eeprom_busy_wait();
						eeprom_update_block(&cm[i], (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES + (i*sizeof(heatfloor_channel_mode)), sizeof(heatfloor_channel_mode));
					}
				}
			}
			
			//send info response
			if (on_heatfloor_mode_message){
				(*on_heatfloor_mode_message)(cm);
			}
			return 1;
		}
	}
	return 0;
}

heatfloor_channel_mode* heatfloor_dispatcher_mode_info(){
	return (heatfloor_channel_mode*)(&channel_modes_buf[0]);
}

void heatfloor_dispatcher_set_on_mode_message(void(*f)(heatfloor_channel_mode* modes)){
	on_heatfloor_mode_message = f;
}

void heatfloor_dispatcher_init(void(*f_request_systime)(void (*f_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week))){
	
	eeprom_read_block(&channel_modes_buf, (void *)EEPROM_ADDRESS_HEATFLOOR_CHANNEL_MODES, sizeof(channel_modes_buf));
	
	time.day_of_week = 0;	//undefined time
	
	//apply and request for current time
	on_heatfloor_dispather_request_systime = f_request_systime;
	requestSystime();
}