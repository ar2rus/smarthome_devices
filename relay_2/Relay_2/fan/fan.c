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
#include <avr/eeprom.h>

void (*on_fan_humidity_request)( void (*fan_humidity_asynch_response)(signed char humidityValue) );
void (*on_fan_control_changed)(char on_);
void (*on_fan_state_changed)(char mode, char state);

volatile char fan_auto_enabled = 0;					 //признак активности автомат. режима

volatile char fan_state = FAN_STATE_WAITING;		 //текущее состояние
volatile char fan_normal_humidity = 0;				 //нормальное (аналогичное тому, что было до резкого его увеличения) значение влажности

char fan_history_humidity[HUMIDITY_HISTORY_COUNT];	//хранит историю измерений влажности (в 0-ом индексе самое "свежее", в (HUMIDITY_HISTORY_COUNT-1) - самое давнее)
volatile char fan_avg_hisory_humidity = 0;			//хранит среднее значение влажности по выборке fan_history_humidity

volatile unsigned int h_counter = 0;				//посекундный счетчик таймера для периодического запроса текущего значения влажности
volatile unsigned int f_counter = 0;				//посекундный счетчик таймера работы вентилятора



void fan_reset_auto_mode(){
	for(unsigned char i = 0; i < HUMIDITY_HISTORY_COUNT; i++) {
		fan_history_humidity[i] = 0x00;
	}
	
	fan_avg_hisory_humidity = 0;
	fan_normal_humidity = 0;
	
	h_counter = 0;
}


void fan_refresh(char event_){
	char fan_state_tmp = fan_state;
	switch (event_){
		case FAN_ACTION_ENABLE_AUTO:
			if (!fan_auto_enabled){
				fan_auto_enabled = 1;
				
				fan_reset_auto_mode();
				
				//save mode in eeeprom
				eeprom_write_byte((void*)FAN_CONFIG_MODE_EEPROM_ADDRESS, fan_auto_enabled); // Режим 
			}
		break;
		case FAN_ACTION_DISABLE_AUTO:
			if (fan_auto_enabled){
				fan_auto_enabled = 0;
				switch(fan_state){	//срубаем автомат. режим
					case FAN_STATE_REQUIRED:
					case FAN_STATE_IN_PROGRESS_AUTO:
						fan_normal_humidity = 0;
						fan_state_tmp = FAN_STATE_WAITING;
					break;
				}
				
				//save mode in eeeprom
				eeprom_write_byte((void*)FAN_CONFIG_MODE_EEPROM_ADDRESS, fan_auto_enabled); // Режим
			}
		break;
		case FAN_ACTION_TRIGGER_TOGGLE_MANUAL:
			fan_normal_humidity = 0;
			switch(fan_state){
				case FAN_STATE_WAITING:
				case FAN_STATE_REQUIRED:
					f_counter = 0;
					fan_state_tmp = FAN_STATE_IN_PROGRESS_MANUAL;
				break;
				case FAN_STATE_IN_PROGRESS_AUTO:
				case FAN_STATE_IN_PROGRESS_MANUAL:
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
				case FAN_STATE_IN_PROGRESS_AUTO:
					fan_normal_humidity = 0;
					fan_state_tmp = FAN_STATE_WAITING;
				break;
			}
		break;
		case FAN_ACTION_TRIGGER_OFF_AUTO:
			switch (fan_state){
				case FAN_STATE_IN_PROGRESS_AUTO:
					fan_state_tmp = FAN_STATE_REQUIRED;
				break;
			}
		break;
		case FAN_ACTION_TRIGGER_ON_AUTO:
			switch (fan_state){
				case FAN_STATE_REQUIRED:
					f_counter = 0;
					fan_state_tmp = FAN_STATE_IN_PROGRESS_AUTO;
				break;
			}
		break;
		case FAN_ACTION_TIMER:
			switch (fan_state){
				case FAN_STATE_REQUIRED:
				case FAN_STATE_IN_PROGRESS_AUTO:
				case FAN_STATE_IN_PROGRESS_MANUAL:
					fan_normal_humidity = 0;
					fan_state_tmp = FAN_STATE_WAITING;
				break;
			}
		break;
	}
	
	
	//now apply state changed only
	if (fan_state_tmp != fan_state){
		char fan_relay_state = fan_state == FAN_STATE_IN_PROGRESS_MANUAL || fan_state == FAN_STATE_IN_PROGRESS_AUTO;		//признак реле работает/нет
		fan_state = fan_state_tmp;
		
		if (on_fan_state_changed){
			(*on_fan_state_changed)(fan_auto_enabled, fan_state);
		}
		
		//управление реле и сброс
		switch(fan_state){
			case FAN_STATE_WAITING:
			case FAN_STATE_REQUIRED:
			
				//send relay off
				if (fan_relay_state){	//исключаем повторную отправку на отключение при переходе FAN_STATE_WAITING->FAN_STATE_REQUIRED и наоборот
					if (on_fan_control_changed){
						(*on_fan_control_changed)(0);
					}
				}
			break;
			case FAN_STATE_IN_PROGRESS_MANUAL:
			case FAN_STATE_IN_PROGRESS_AUTO:
				
				//send relay on
				if (!fan_relay_state){	//исключаем повторную отправку на отключение при переходе FAN_STATE_MANUAL->FAN_STATE_AUTO и наоборот
					if (on_fan_control_changed){
						(*on_fan_control_changed)(1);
					}
				}
			break;
		}
	}else{	//шлем в любом случае сообщение, если был вызов команды установки режима
		if ((event_==FAN_ACTION_ENABLE_AUTO) || (event_ == FAN_ACTION_DISABLE_AUTO)){
			if (on_fan_state_changed){
				(*on_fan_state_changed)(fan_auto_enabled, fan_state);
			}
		}
	}
}


void fan_humidity(signed char humidityValue){
	if (fan_auto_enabled){
		fan_history_humidity[0] = humidityValue;
		
		//здесь определяется динамика роста уровня влажности
		if (humidityValue > 0 && fan_avg_hisory_humidity > 0){
			//сначала уточним есть ли резкий прирост значения влажности
			signed char delta = humidityValue - fan_avg_hisory_humidity;
			if (delta >= HUMIDITY_DELTA_PLUS){
				fan_refresh(FAN_ACTION_SENSOR_RISED);
				return;
				}else if (fan_normal_humidity  >= humidityValue){
				//влажность уменьшена до нормального значения -> вырубаем вентилятор
				fan_refresh(FAN_ACTION_SENSOR_NORMALIZED);
				return;
			}
		}
		
		//наименее приоритетное срабатывание по абсолютному значению
		//имеет смысл при запуске устройства сразу в условиях высокой влажности
		//в этом случае мы не можем использовать fan_normal_humidity равный такому значению
		//и вентилятор в этом случае должен отработать полный цикл
		if (humidityValue >= HUMIDITY_MAX_ABS){
			fan_refresh(FAN_ACTION_SENSOR_MAX_ABS);
		}
	}
}

void fan_tick_second(){
	if (fan_auto_enabled){
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
 				(*on_fan_humidity_request)( fan_humidity );
  			}
  		}
	}
	
 	if (fan_state == FAN_STATE_IN_PROGRESS_AUTO || fan_state == FAN_STATE_IN_PROGRESS_MANUAL){
 		if (++f_counter > FAN_TIMEOUT){
 			fan_refresh(FAN_ACTION_TIMER);
 		}
 	}
}

void fan_mode(char enable){
	fan_refresh(enable ? FAN_ACTION_ENABLE_AUTO : FAN_ACTION_DISABLE_AUTO);
}

void fan_button(){
	fan_refresh(FAN_ACTION_TRIGGER_TOGGLE_MANUAL);
}

void fan_info(struct fan_info_struct* i){
	i->mode = fan_auto_enabled;
	i->state = fan_state;
	i->normal_humidity = fan_normal_humidity;
	memcpy(i->history_humidity, fan_history_humidity, sizeof(fan_history_humidity));
	i->timer_remains = FAN_TIMEOUT - f_counter;
}

void fan_trigger(char on_){
	if (fan_auto_enabled){
		fan_refresh(on_ ? FAN_ACTION_TRIGGER_ON_AUTO : FAN_ACTION_TRIGGER_OFF_AUTO);
	}
}

void fan_set_on_state_changed(void (*f)(char mode, char state)){
	on_fan_state_changed = f;
}


void fan_init(void (*fan_humidity_request)( void (*fan_humidity_asynch_response)(signed char humidityValue) ), void (*fan_control_change_request)(char on_)){
	
	on_fan_humidity_request = fan_humidity_request;
	on_fan_control_changed = fan_control_change_request;
	
	//read eeprom settings:
	char auto_enabled_config = eeprom_read_byte((void*)FAN_CONFIG_MODE_EEPROM_ADDRESS);
	fan_refresh(auto_enabled_config ? FAN_ACTION_ENABLE_AUTO : FAN_ACTION_DISABLE_AUTO);
	
	//read state from eeprom:
	//FAN_TIMEOUT
	//HUMIDITY_DELTA_PLUS
	//FAN_HUMIDITY_CHECK_TIME ?
}