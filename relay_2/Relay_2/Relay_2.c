
#include "Relay_2.h"

volatile unsigned char systime = 0;

signed char charger_task = -1;

//храним состояние дверей шкафчика, для 
//того чтобы правильно реагировать на кнопку
signed char door_state = -1;

// храним состояние кнопки (-1 - не инициализирована)
signed char button_state = -1;


void (*timer_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week) = NULL;

void timer_systime_request( void (*f)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week) ){
	//пришел запрос на получение нового значения текущего времени
	//в параметре - функция ответа (асинхронно)
	timer_systime_async_response = f;
	clunet_send_fairy(CLUNET_SUPRADIN_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_TIME, 0, 0);	//ask for supradin clients only!!!
}


void switchExecute(char id, char command){
	switch(command){
		case 0x00:	//откл
		switch(id){
			case RELAY_0_ID:
				RELAY_0_OFF;
			break;
			case RELAY_1_ID:
				RELAY_1_OFF;
			break;
			case RELAY_2_ID:
				RELAY_2_OFF;
			break;
		}
		break;
		case 0x01: //вкл
		switch(id){
			case RELAY_0_ID:
				RELAY_0_ON;
			break;
			case RELAY_1_ID:
				RELAY_1_ON;
			break;
			case RELAY_2_ID:
				RELAY_2_ON;
			break;
		}
		break;
		case 0x02: //перекл
		switch(id){
			case RELAY_0_ID:
				RELAY_0_TOGGLE;
			break;
			case RELAY_1_ID:
				RELAY_1_TOGGLE;
			break;
			case RELAY_2_ID:
				RELAY_2_TOGGLE;
			break;
		}
		break;
	}
}

void switchResponse(unsigned char address){
	char info = (RELAY_2_STATE << (RELAY_2_ID-1)) | (RELAY_1_STATE << (RELAY_1_ID-1)) | (RELAY_0_STATE << (RELAY_0_ID-1));
	clunet_send_fairy(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_SWITCH_INFO, &info, sizeof(info));
}

void chargeResponse(unsigned char address){
	char info[3];
	info[0] = TOOTHBRUSH_RELAY_STATE;
	if (info[0]){
		if (charger_task >= 0){
			task* t = timer_get_task(charger_task);
			if (t){
				info[1] = t->seconds;
				info[2] = (t->seconds >> 8);
			}
		}
	}
	clunet_send_fairy(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_CHARGE_INFO, info, info[0] ? 3 : 1);
}


void (*fan_humidity_response)(signed char humidityValue) = NULL;

void fan_humidity_request( void (*f)(signed char humidityValue) ){
	//пришел запрос на получение нового значения влажности
	//в параметре - функция ответа (асинхронного)
	fan_humidity_response = f;
	clunet_send_fairy(HUMIDITY_SENSOR_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_HUMIDITY, 0, 0);
}

void fan_control_changed(char on_){
	//не вызывать switchResponse из switchExecute
	//для возможности первоначального переключения нескольких ключей сначала
	//а потом отправки одного респонса на всех
	switchExecute(FAN_RELAY_ID, on_);
	switchResponse(CLUNET_BROADCAST_ADDRESS);
}

void fanResponse(unsigned char address){
	struct fan_info_struct i;
	fan_info(&i);
	clunet_send_fairy(address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_FAN_INFO, (char*)&i, sizeof(i));
}

void fan_state_changed(char mode, char state){
	fanResponse(CLUNET_BROADCAST_ADDRESS);
}

void cmd(clunet_msg* m){
	switch(m->command){
		
		case CLUNET_COMMAND_SWITCH:
			switch(m->size){
				case 1:
					if (m->data[0] == 0xFF){
						switchResponse(m->src_address);
					}
					break;
				case 2:
					switch(m->data[0]){
						case 0x00:
						case 0x01:
						case 0x02:
							switchExecute(m->data[1], m->data[0]);
							switchResponse(m->src_address);
							break;
						case 0x03:
							for (char i=0; i<8; i++){
								switchExecute((i+1), bit(m->data[1], i));
							}
							switchResponse(m->src_address);
							break;
					}
					break;
			}
			break;
		
		case CLUNET_COMMAND_HUMIDITY_INFO:
			if (fan_humidity_response != NULL){
				if (m->src_address == HUMIDITY_SENSOR_DEVICE_ID && m->dst_address == CLUNET_DEVICE_ID){
					if (m->size == 2){
						signed int h10 = *(signed int*)m->data;
						if (h10 < 0xFFFF){
							h10 /= 10;
							fan_humidity_response(h10 & 0xFF);	
							
							fan_humidity_response = NULL;
						}
					}
				}
			}
			break;
		
		case CLUNET_COMMAND_LIGHT_LEVEL_INFO:
			if (m->src_address == LIGHT_SENSOR_DEVICE_ID && m->size==2){
				fan_trigger(!(m->data[0]));
			}
			break;
			
		case CLUNET_COMMAND_DOOR_INFO:
			if (m->src_address == DOORS_MIRRORED_BOX_DEVICE_ID && m->size==1){
				door_state = m->data[0];
				switchExecute(MIRRORED_BOX_LIGHT_RELAY_ID, door_state > 0);
				switchResponse(CLUNET_BROADCAST_ADDRESS);
			}
			break;
			
		case CLUNET_COMMAND_FAN:
			if (m->size == 1){
				switch (m->data[0]){
					case 0x00:
					case 0x01:
						fan_mode(m->data[0]);	//вкл/выкл авто режим
						break;
					case 0x02:
						fan_button();			//переключение в ручном режиме
						break;
					case 0xFF:
						fanResponse(m->src_address);
						break;
				}
			}
			break;
		case CLUNET_COMMAND_RC_BUTTON_PRESSED:
			if (m->size == 3){
				if (m->data[0] == 0x00 && m->data[1] == 0x00){
					if (m->data[2] == 0x4A){
						fan_button();
					}
				}
			}
			break;
		case CLUNET_COMMAND_TIME: {
				datetime* dt = timer_systime();
				
				char hd[7] = {0, 1, 1, dt->hours, dt->minutes, dt->seconds, dt->day_of_week};
				clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_TIME_INFO, &hd[0], sizeof(hd));
			}
			break;
		case CLUNET_COMMAND_TIME_INFO:
			if (timer_systime_async_response != NULL){
				if (m->size == 7 && m->src_address == CLUNET_SUPRADIN_ADDRESS){
					timer_systime_async_response(m->data[5], m->data[4], m->data[3], m->data[6]);
					timer_systime_async_response = NULL;
				}
			}
			break;
		case CLUNET_COMMAND_CHARGE:
			switch(m->size){
				case 1:
				switch (m->data[0]){
					case 0x00:
						stop_charge();
						break;
					case 0xFF:
						chargeResponse(m->src_address);
						break;
				}
				break;
				case 3:
				if (m->data[0] == 0x01){	//on
					start_charge(*(unsigned int*)&(m->data[1]), 0);
					//chargeResponse(m->src_address);
				}
				break;
			}
			break;
		case CLUNET_COMMAND_BUTTON_INFO:
			if (m->src_address == BUTTON_DEVICE_ID && m->size==2){
				if (button_state >= 0){
					if (button_state != m->data[1]){
						if (door_state){	//открыта -> зарядка (430 секунд)
							char data[3];
							data[0] = 0x01;
							data[1] = 0xAE;
							data[2] = 0x01;	
							clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_CHARGE, data, sizeof(data));
						}else{	// переключаем свет
							char data[2];
							data[0] = 0x02;	//toggle
							data[1] = MIRRORED_BOX_LIGHT_RELAY_ID;
							clunet_buffered_push(CLUNET_BROADCAST_ADDRESS, CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_SWITCH, data, sizeof(data));
						}
					}
				}
				button_state = m->data[1];
			}
			break;
			case CLUNET_COMMAND_DEBUG:{
				char data[2];
				data[0] = door_state;
				data[1] = button_state;
				clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_DEBUG, data, 2);
			}
	}
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_SWITCH:
		case CLUNET_COMMAND_HUMIDITY_INFO:
		case CLUNET_COMMAND_LIGHT_LEVEL_INFO:
		case CLUNET_COMMAND_DOOR_INFO:
		case CLUNET_COMMAND_FAN:
		case CLUNET_COMMAND_RC_BUTTON_PRESSED:
		case CLUNET_COMMAND_TIME:
		case CLUNET_COMMAND_TIME_INFO:
		case CLUNET_COMMAND_CHARGE:
		case CLUNET_COMMAND_BUTTON_INFO:
		case CLUNET_COMMAND_DEBUG:
			clunet_buffered_push(src_address, dst_address, command, data, size);
	}
}

void stop_charge(){
	if (charger_task >= 0){
		timer_remove_task(charger_task);
		charger_task = -1;
	}
	
	switchExecute(TOOTHBRUSH_RELAY_ID, 0x00);
	switchResponse(CLUNET_BROADCAST_ADDRESS);
	
	chargeResponse(CLUNET_BROADCAST_ADDRESS);
}

void start_charge_and_schedule_next(){
	start_charge(CHARGE_DURATION, 1);
}

void schedule_next_charge(){
	timer_add_scheduled_task(CHARGE_DAY_OF_WEEK, CHARGE_HOUR, CHARGE_MINUTE, start_charge_and_schedule_next);	//пн, 10:00
}

void stop_charge_and_schedule_next(){
	stop_charge();
	schedule_next_charge();
}

void start_charge(unsigned int num_seconds, unsigned char schedule){
	if (num_seconds > 0){
		if (schedule){	//пришло время длинной запланированной зарядки -> срубаем все текущие
			if (charger_task >= 0){
				stop_charge();
			}
		}
	
		//короткие зарядки не срубают длинную запланированную
		if (charger_task < 0){
			if (schedule){
				charger_task = timer_add_countdown_task(num_seconds, stop_charge_and_schedule_next);
			}else{
				charger_task = timer_add_countdown_task(num_seconds, stop_charge);
			}
		
			switchExecute(TOOTHBRUSH_RELAY_ID, 0x01);
			switchResponse(CLUNET_BROADCAST_ADDRESS);
		}
		chargeResponse(CLUNET_BROADCAST_ADDRESS);
	}
}



ISR(TIMER_COMP_VECTOR){
	++systime;
	
	TIMER_REG = 0;	//reset counter
}


unsigned char prev_systime = 0;

int main(void){
	cli();
	
	RELAY_0_INIT;
	RELAY_1_INIT;
	RELAY_2_INIT;
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_buffered_init();
	clunet_init();

	
	fan_set_on_state_changed(fan_state_changed);		//for debugging and logging
	fan_init(fan_humidity_request, fan_control_changed);
	
	
	timer_init(timer_systime_request);
	schedule_next_charge();				//по воскресеньям в 1 ночи
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;
	
	//ask for door state
	clunet_send_fairy(DOORS_MIRRORED_BOX_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_DOOR, 0, 0);
	//ask for button state
	clunet_send_fairy(BUTTON_DEVICE_ID, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_BUTTON, 0, 0);
	
	while (1){
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
		
		if (prev_systime != systime){
			prev_systime = systime;
			fan_tick_second();
			timer_tick_second();
		}
	}
	
	return 0;
}