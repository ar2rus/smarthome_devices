/*
 * fan.c
 *
 * Created: 21.09.2015 15:49:35
 *  Author: gargon
 */ 

#include "fan.h"

#include <avr/io.h>
#include <string.h>
#include <avr/interrupt.h>

void (*on_fan_humidity_request)();
void (*on_fan_control_changed)(char on_);
void (*on_fan_state_changed)(char state);


volatile char fan_state = FAN_STATE_WAITING;		 //������� ���������
volatile char fan_normal_humidity = 0;				 //���������� (����������� ����, ��� ���� �� ������� ��� ����������) �������� ���������

char fan_history_humidity[HUMIDITY_HISTORY_COUNT];	//������ ������� ��������� ��������� (� 0-�� ������� ����� "������", � (HUMIDITY_HISTORY_COUNT-1) - ����� ������)
volatile char fan_avg_hisory_humidity = 0;			//������ ������� �������� ��������� �� ������� fan_history_humidity

volatile unsigned int h_counter = 0;		//����������� ������� ������� ��� �������������� ������� �������� �������� ���������
volatile unsigned int f_counter = 0;		//����������� ������� ������� ������ �����������


void fan_refresh(char event_){
	char fan_state_tmp = fan_state;
	switch (event_){
		case FAN_ACTION_BUTTON:
			fan_normal_humidity = 0;
			switch(fan_state){
				case FAN_STATE_WAITING:
				case FAN_STATE_REQUIRED:
					f_counter = 0;
					fan_state_tmp = FAN_STATE_MANUAL;
				break;
				case FAN_STATE_AUTO:
				case FAN_STATE_MANUAL:
					fan_state_tmp = FAN_STATE_WAITING;
				break;
			}
		break;
		case FAN_ACTION_SENSOR_RISED:
			switch(fan_state){
				case FAN_STATE_WAITING:
					//save normal(avg) humidity
					fan_normal_humidity = fan_avg_hisory_humidity;
					fan_state_tmp = FAN_STATE_REQUIRED;
				break;
			}
		break;
		case FAN_ACTION_SENSOR_MAX_ABS:
			switch(fan_state){
				case FAN_STATE_WAITING:
					fan_normal_humidity = 0;
					fan_state_tmp = FAN_STATE_REQUIRED;
				break;
			}
		break;
		case FAN_ACTION_SENSOR_NORMALIZED:
			switch(fan_state){
				case FAN_STATE_REQUIRED:
				case FAN_STATE_AUTO:
					fan_normal_humidity = 0;
					fan_state_tmp = FAN_STATE_WAITING;
				break;
			}
		break;
		case FAN_ACTION_LIGHT_ON:
			switch (fan_state){
				case FAN_STATE_AUTO:
					fan_state_tmp = FAN_STATE_REQUIRED;
				break;
			}
		break;
		case FAN_ACTION_LIGHT_OFF:
			switch (fan_state){
				case FAN_STATE_REQUIRED:
					f_counter = 0;
					fan_state_tmp = FAN_STATE_AUTO;
				break;
			}
		break;
		case FAN_ACTION_TIMER:
			switch (fan_state){
				case FAN_STATE_REQUIRED:
				case FAN_STATE_AUTO:
				case FAN_STATE_MANUAL:
					fan_normal_humidity = 0;
					fan_state_tmp = FAN_STATE_WAITING;
				break;
			}
		break;
	}
	
	if (fan_state_tmp != fan_state){
		
		char fan_relay_state = fan_state == FAN_STATE_MANUAL || fan_state == FAN_STATE_AUTO;		//������� ���� ��������/���
		fan_state = fan_state_tmp;
		
		if (on_fan_state_changed){
			(*on_fan_state_changed)(fan_state);
		}

		//���������� ���� � �����
		switch(fan_state){
			case FAN_STATE_WAITING:
			case FAN_STATE_REQUIRED:
			
				//send relay off
				if (fan_relay_state){	//��������� ��������� �������� �� ���������� ��� �������� FAN_STATE_WAITING->FAN_STATE_REQUIRED � ��������
					if (on_fan_control_changed){
						(*on_fan_control_changed)(0);
					}
				}
			break;
			case FAN_STATE_MANUAL:
			case FAN_STATE_AUTO:
				
				//send relay on
				if (!fan_relay_state){	//��������� ��������� �������� �� ���������� ��� �������� FAN_STATE_MANUAL->FAN_STATE_AUTO � ��������
					if (on_fan_control_changed){
						(*on_fan_control_changed)(1);
					}
				}
			break;
		}
	}
}


ISR(FAN_TIMER_COMP_VECTOR){
	sei();
	
	FAN_TIMER_REG = 0;	//reset counter
	
  	if (++h_counter >= FAN_HUMIDITY_CHECK_TIME){
  		h_counter = 0;
  		
  		//shift history right and fill the new first value with 0
		//calc avg by history
		
		char cnt = 0;
		int sum = 0;
		
  		for(signed char i = HUMIDITY_HISTORY_COUNT-2; i >= 0; i--) {
			if (fan_history_humidity[i+1] > 0){
				sum += fan_history_humidity[i+1];
				++cnt;
			}
			fan_history_humidity[i+1] = fan_history_humidity[i];
		}
		
		if (fan_history_humidity[0] > 0){
			sum += fan_history_humidity[0];
			++cnt;
			
			fan_history_humidity[0] = 0x00;
		}
		
		fan_avg_hisory_humidity = 0;
		if (cnt > 0){
			fan_avg_hisory_humidity = sum / cnt;
		}
		  
		//send request for humidity
  		if (on_fan_humidity_request){
 			(*on_fan_humidity_request)();
  		}
  	}
	
 	if (fan_state == FAN_STATE_AUTO || fan_state == FAN_STATE_MANUAL){
 		if (++f_counter > FAN_TIMEOUT){
 			fan_refresh(FAN_ACTION_TIMER);
 		}
 	}
}

void fan_init(){
	for(unsigned char i = 0; i < HUMIDITY_HISTORY_COUNT; i++) {
		fan_history_humidity[i] = 0x00;
	}
	
	FAN_TIMER_INIT;
	ENABLE_FAN_TIMER_CMP_A;
}

void fan_button(){
	fan_refresh(FAN_ACTION_BUTTON);
}

void fan_humidity(signed char humidityValue){
	fan_history_humidity[0] = humidityValue;
	
	//����� ������������ �������� ����� ������ ���������
	if (humidityValue > 0 && fan_avg_hisory_humidity > 0){
		//������� ������� ���� �� ������ ������� �������� ���������
		signed char delta = humidityValue - fan_avg_hisory_humidity;
		if (delta >= HUMIDITY_DELTA_PLUS){
			fan_refresh(FAN_ACTION_SENSOR_RISED);
			return;
		}else if (fan_normal_humidity  >= humidityValue){
			//��������� ��������� �� ����������� �������� -> �������� ����������
			fan_refresh(FAN_ACTION_SENSOR_NORMALIZED);
			return;
		}
	}
	
	//�������� ������������ ������������ �� ����������� ��������
	//����� ����� ��� ������� ���������� ����� � �������� ������� ���������
	//� ���� ������ �� �� ����� ������������ fan_normal_humidity ������ ������ ��������
	//� ���������� � ���� ������ ������ ���������� ������ ����
	if (humidityValue >= HUMIDITY_MAX_ABS){
		fan_refresh(FAN_ACTION_SENSOR_MAX_ABS);
	}
}

void fan_info(struct fan_info_struct* i){
	i->state = fan_state;
	i->normal_humidity = fan_normal_humidity;
	memcpy(i->history_humidity, fan_history_humidity, sizeof(fan_history_humidity));
	i->timer_remains = FAN_TIMEOUT - f_counter;
}

void fan_light(char on_){
	fan_refresh(on_ ? FAN_ACTION_LIGHT_ON : FAN_ACTION_LIGHT_OFF);
}

void fan_set_on_humidity_request(void (*f)()){
	on_fan_humidity_request = f;
}

void fan_set_on_control_changed(void (*f)(char on_)){
	on_fan_control_changed = f;
}

void fan_set_on_state_changed(void (*f)(char state)){
	on_fan_state_changed = f;
}