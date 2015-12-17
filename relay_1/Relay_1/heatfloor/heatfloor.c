/*
 * heatfloor.c
 *
 * Created: 12.10.2015 15:08:13
 *  Author: gargon
 */ 

#include "heatfloor.h"

#include <avr/io.h>
#include <avr/interrupt.h>

signed int (*on_heatfloor_sensor_temperature_request)(unsigned char channel) = 0;
char (*on_heatfloor_switch_exec)(unsigned char channel, unsigned char on_) = 0;
void (*on_heatfloor_state_message)(heatfloor_channel_infos* infos) = 0;


unsigned char enable;
unsigned char sensorCheckTimer;

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
	
	if (enable){
		for (unsigned char i = 0; i < 8; i++){
			if (test_bit(enable, i)){
				
				signed int settingT = -1;
				signed int sensorT = -1;
				
				if (on_heatfloor_sensor_temperature_request){
					sensorT = (*on_heatfloor_sensor_temperature_request)(i);
				}
				
				settingT = heatfloor_dispatcher_resolve_temperature_setting(i);
				
				signed char solution = 0;
				
				if (settingT > 0){
					if (sensorT >= 0){
						//check range
						if (sensorT >= HEATFLOOR_MIN_TEMPERATURE_10 && sensorT <= HEATFLOOR_MAX_TEMPERATURE_10){
							if (sensorT <= settingT - HEATFLOOR_SENSOR_HYSTERESIS_TEMPERATURE_10){
								solution = 1;					//нагреваемся
							}else if (sensorT >= settingT){
								solution = -1;					//остужаемся
							}
								//в случае если значение попадает в необходимый диапазон, то
								//сохраняем состояние: удерживаем нагревание, пока не выйдет за верхнюю границу
								//или удерживаем охлаждение, пока не выйдем за нижнюю границу
								
								//при уставке 28 и гистерезисе 0.3 - будем разогреваться до 28(и еще инерция порядка 0,3) и охлаждаться до 27.7
						}else{
							solution = -3;						    //ошибка диапазона значения с датчика (85*, например)
						}
					}else{
						solution = -2;								//ошибка чтения значения с датчика
					}
				}else{
					solution = -4;									//ошибка получения значения от диспетчера
				}
				
				if (solution != 0){
					heatfloor_turnSwitch(i, solution>0);
				}
				
				//тут накапливаем ответ по всем каналам
				heatfloor_channel_info* ci = &cis->channels[(cis->num)++];
				ci->num = i;
				ci->solution = solution;
				ci->sensorT = sensorT;
				ci->settingT = enable/*settingT*/;
				
			}
		}
	}
	return cis;
}

//вызывает рефреш и генерирует события ответа
//response_required - генерировать событие всегда или только при наличии активных каналов
void heatfloor_refresh_responsible(unsigned char response_required){
	heatfloor_channel_infos* infos = heatfloor_refresh();
	if (infos->num || response_required){	//generate an event if we have active channels only
		if (on_heatfloor_state_message){
			(*on_heatfloor_state_message)(infos);
		}
	}
}

ISR(HEATFLOOR_CONTROLLER_TIMER_vect){
	if (++sensorCheckTimer >= HEATFLOOR_SENSOR_CHECK_TIME){
		heatfloor_refresh_responsible(0);	//отправим ответ только если есть активные каналы
	}
	HEATFLOOR_CONTROLLER_TIMER_REG_OCR = HEATFLOOR_CONTROLLER_TIMER_REG + HEATFLOOR_CONTROLLER_TIMER_NUM_TICKS;
}


void heatfloor_set_on_state_message(void(*f)(heatfloor_channel_infos* infos)){
	on_heatfloor_state_message = f;
}

void heatfloor_init(
		signed int (*f_sensor_temperature_request)(unsigned char channel),
		char (*f_switch_exec)(unsigned char channel, unsigned char on_),
		void (*f_request_systime)()
		){
			
	enable = 0;
	
	on_heatfloor_sensor_temperature_request = f_sensor_temperature_request;
	on_heatfloor_switch_exec = f_switch_exec;
	
	heatfloor_dispatcher_init(f_request_systime);
	
	HEATFLOOR_CONTROLLER_TIMER_INIT;
	ENABLE_HEATFLOOR_CONTROLLER_TIMER;
}

void heatfloor_enable(unsigned char channel, unsigned char enable_){
	if (enable_ != bit(enable, channel)){
		if (!enable_){
			//switch relay off
			heatfloor_turnSwitch(channel, 0);
		}
		flip_bit(enable, channel);
		heatfloor_refresh_responsible(1);	//отправим ответ всегда, даже если отключили последний канал
	}
}

void heatfloor_set_systime(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week){
	heatfloor_dispatcher_set_systime(seconds,minutes,hours,day_of_week);
}