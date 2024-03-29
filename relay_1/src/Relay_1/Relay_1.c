
#include "Relay_1.h"

#include <avr/wdt.h>

volatile unsigned char systime = 0;


//���������� ���-�� �������� 1-wire ���������; ������������� ������ 1 ��� ����� �������������.
//���� ��� ����� �� ������������� ����������������, ������� ������� ������� oneWireSearch 

char owi_num_devices = 0;
OWI_device devices[OWI_MAX_BUS_DEVICES];


signed char switchState(unsigned char id){
	switch(id){
		case RELAY_0_ID:
			return RELAY_0_STATE;
		break;
		case RELAY_1_ID:
			return RELAY_1_STATE;
		break;
		case RELAY_2_ID:
			return RELAY_2_STATE;
		break;
	}
	return -1;
}

void switchExecute(unsigned char id, unsigned char command){
	switch(command){
		case 0x00:	//����
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
		case 0x01: //���
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
		case 0x02: //������
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

/*
*	�������������� ����� ��������� �� ���� 1-Wire
*	� ���������� ���-�� ��������� ���������
*	��� -1 � ������ ������
*/
char oneWireSearch(OWI_device* devices){
	
	//����, ������ ������ � 1-wire ��������
	//sei() � cli(), ��� ����� ������� ������� �������� �� clunet (���� ��� ����)
	//������� �������� �� ����� � ������ ���������� � 1-wire
	
	clunet_wait_sending();
	
	unsigned char num = 0;
	if (OWI_SearchDevices(devices, OWI_MAX_BUS_DEVICES, OWI_BUS, &num) == SEARCH_CRC_ERROR){
		return -1;
	}else{
		return num;
	}
}

/*
*  ������ ����������� ������� �� ��� ��������� OWI_device
*  ���������� 1 ���� ������ �������� � 0 - ���� ���
*/
char temperatureRequest(OWI_device* device, signed int* temperature){
	
	clunet_wait_sending();
	
	if (DS18B20_ReadDeviceCache(OWI_BUS, (*device).id, temperature) != READ_CRC_ERROR){
		return 1;
	}
	return 0;
}

/*
* ������ ����������� ��� ��������� devices ������������ � ������ ������ �� clunet
*/
void temperatureResponse(unsigned char address, OWI_device* devices, unsigned char size){
	char temperatureInfo[11 * size + 1];
	
	unsigned char cnt = 0;
	//DS18B20_StartAllDevicesConverting(OWI_BUS);
	for (unsigned char i=0; i<size; i++){
		unsigned char pos = 1 + 11*cnt;
		
		clunet_wait_sending();
		
		if (DS18B20_ReadDeviceCache(OWI_BUS, (*devices).id, (signed int *)&temperatureInfo[pos + 9]) != READ_CRC_ERROR){
			temperatureInfo[pos] = 0; //1-wire
			memcpy(&temperatureInfo[pos + 1], (*devices).id, sizeof((*devices).id));
			cnt++;
		}
		devices++;
	}

	temperatureInfo[0] = cnt;
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_TEMPERATURE_INFO, ((char*)temperatureInfo), 11 * cnt + 1);
}

void heatfloor_states_response(unsigned char address, heatfloor_channel_infos* infos){
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_HEATFLOOR_INFO, ((char*)infos), infos->num * sizeof(heatfloor_channel_info) + 1);
}

void heatfloor_modes_response(unsigned char address, heatfloor_channel_modes* modes){
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_HEATFLOOR_INFO, ((char*)modes), sizeof(heatfloor_channel_modes));
}

void heatfloor_program_response(unsigned char address, heatfloor_program* program){
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_HEATFLOOR_INFO, ((char*)program), sizeof(heatfloor_program));
}



void (*heatfloor_systime_async_response)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week) = NULL;

void heatfloor_systime_request( void (*f)(unsigned char seconds, unsigned char minutes, unsigned char hours, unsigned char day_of_week) ){
	heatfloor_systime_async_response = f;
	clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_TIME, 0, 0);
}

void cmd(clunet_msg* m){
	switch(m->command){
		case CLUNET_COMMAND_SWITCH:
			if (m->data[0] == 0xFF){	//info request
				if (m->size == 1){
					switchResponse(m->src_address);
				}
			}else{
				if (m->size == 2){
					switch(m->data[0]){
						case 0x00:
						case 0x01:
						case 0x02:
							switchExecute(m->data[1], m->data[0]);
							switchResponse(m->src_address);
							break;
						case 0x03:
							for (char i=0; i<8; i++){
								switchExecute(i+1, bit(m->data[1], i));
							}
							switchResponse(m->src_address);
							break;
					}
				}
			}
			break;
		case CLUNET_COMMAND_ONEWIRE_SEARCH:
			if (m->size == 0){
				owi_num_devices = oneWireSearch(devices);
				clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_ONEWIRE_INFO, (char*)&devices, owi_num_devices * sizeof(OWI_device));
			}
			break;
		case CLUNET_COMMAND_TEMPERATURE:
			if (m->size >= 1){
				switch(m->data[0]){
					case 1:	//1-wire ����������
							//���������, ��� ������������� ��� 1-wire ����������
							//� ���� ������������� ��� - �� ��������� ��� ��� data[0] = 0
						if (m->size != 2 || m->data[1] != 0){
							break;
						}
					case 0:	{	//��� ����������
						
							//������ �� ������ ����� ��������� ��� ������ �������,
							//� ���������� ������ ���������� ��� ������ ���������
							//��� ��� ������ ������� oneWireSearch 
							//�.�. �������� ��������� ����������
							if (!owi_num_devices){
								owi_num_devices = oneWireSearch(devices);
							}
							temperatureResponse(m->src_address, devices, owi_num_devices);
						}
						break;
					case 2:		//������ �� ���������
						if (m->size == 10 && m->data[1] == 0){	//������ �� 1-wire
							temperatureResponse(m->src_address, (OWI_device*)&m->data[2], 1);
						}
						break;
				}
			}
			break;
// 		case CLUNET_COMMAND_DOOR_INFO:
// 			if (m->src_address == DOORS_SENSOR_DEVICE_ID){
// 				if (m->size==1 && m->data[0] >=0){
// 					unsigned char doors_opened = m->data[0]>0;
// 					signed char state = switchState(WARDROBE_LIGHT_RELAY_ID);
// 					if (state >= 0 && state != doors_opened){
// 						switchExecute(WARDROBE_LIGHT_RELAY_ID, doors_opened);
// 						switchResponse(CLUNET_BROADCAST_ADDRESS);
// 					}
// 				}
// 			}
// 			break;
		case CLUNET_COMMAND_HEATFLOOR:
			switch (m->size){
				case 0x01:
					switch(m->data[0]){
						case 0x00:
						case 0x01:
							heatfloor_on(m->data[0]);
							break;
						case 0xFF:	// ������ �������� ��������� ���� �������
							heatfloor_states_response(m->src_address, heatfloor_state_info());
							break;
						case 0xFE:	//������ ������� ������� �� ���� �������
							heatfloor_modes_response(m->src_address, heatfloor_modes_info());
							break;
						
						case 0xF0 ... 0xF9:	//������ ���������� ��������� (0-9)
							heatfloor_program_response(m->src_address, heatfloor_program_info(m->data[0]));
						
						//case 0xFE:	//setup ds18b20 (temporary)
							//	DS18B20_SetDeviceAccuracy(OWI_BUS, &HEATING_FLOOR_CHANNEL_0_SENSOR_1W_ID, 3);
							//	break;
					}
					break;
				default:
					heatfloor_command(m->data, m->size);	//���������� ����������
					break;
			}
			break;
		case CLUNET_COMMAND_TIME: {
			heatfloor_datetime* dt = heatfloor_systime();
			
			char hd[7] = {0, 1, 1, dt->hours, dt->minutes, dt->seconds, dt->day_of_week};
			clunet_send_fairy(m->src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_TIME_INFO, &hd[0], sizeof(hd));
			}
			break;
		case CLUNET_COMMAND_TIME_INFO:
			if (heatfloor_systime_async_response != NULL){
				if (m->size == 7 && CLUNET_MULTICAST_DEVICE(m->src_address)){
					heatfloor_systime_async_response(m->data[5], m->data[4], m->data[3], m->data[6]);
					heatfloor_systime_async_response = NULL;
				}
			}
			break;
	}
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_SWITCH:
		case CLUNET_COMMAND_ONEWIRE_SEARCH:
		case CLUNET_COMMAND_TEMPERATURE:
		//case CLUNET_COMMAND_DOOR_INFO:
		case CLUNET_COMMAND_HEATFLOOR:
		case CLUNET_COMMAND_TIME:
		case CLUNET_COMMAND_TIME_INFO:
			clunet_buffered_push(src_address, dst_address, command, data, size);
	}
}

signed int heatfloor_sensor_temperature_request(unsigned char channel){
	
	OWI_device* device = NULL;
	
	switch (channel){
		case HEATING_FLOOR_CHANNEL_0:
			device =  (OWI_device*)&HEATING_FLOOR_CHANNEL_0_SENSOR_1W_ID;
		break;
		case HEATING_FLOOR_CHANNEL_1:
			device =  (OWI_device*)&HEATING_FLOOR_CHANNEL_1_SENSOR_1W_ID;
		break;
	}
	if (device){
		
		//������ ��������� ������ oneWire �������� sei() � cli(),
		//������� ����� ������� ���������� ������ clunet, �������
		//�� ������� ���� �������� ��������� ������� ��������, ���� ��� �������
		
		signed int t;
		if (temperatureRequest(device, &t)){
			return t;
		}
	}
	return -1;
}

void heatfloor_control_switch_request(unsigned char channel, unsigned char on_){
	signed char id= -1;
	switch (channel){
		case HEATING_FLOOR_CHANNEL_0:
			id = HEATING_FLOOR_CHANNEL_0_RELAY_ID;
		break;
		case HEATING_FLOOR_CHANNEL_1:
			id = HEATING_FLOOR_CHANNEL_1_RELAY_ID;
		break;
	}
	
	if (id >=0){
		signed char state = switchState(id);
		if (state >=0 && state != on_){
			switchExecute(id, on_);
			switchResponse(CLUNET_BROADCAST_ADDRESS);
		}
	}
}

void heatfloor_states_message(heatfloor_channel_infos* infos){
	heatfloor_states_response(CLUNET_BROADCAST_ADDRESS, infos);
}

void heatfloor_modes_message(heatfloor_channel_modes* modes){
	heatfloor_modes_response(CLUNET_BROADCAST_ADDRESS, modes);
}

void heatfloor_program_message(heatfloor_program* program){
	heatfloor_program_response(CLUNET_BROADCAST_ADDRESS, program);
}

ISR(TIMER_COMP_VECTOR){
	++systime;
	
	TIMER_REG = 0;	//reset counter
}



unsigned char cur_time = 0;

int main(void){
	wdt_disable(); 
	cli();
	
	RELAY_0_INIT;
	RELAY_1_INIT;
	RELAY_2_INIT;
	
	OWI_Init(OWI_BUS);
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_init();
	
	heatfloor_init(
			heatfloor_sensor_temperature_request,
			heatfloor_control_switch_request, 
			heatfloor_systime_request
			);
			
			heatfloor_set_on_states_message(heatfloor_states_message);
			heatfloor_set_on_modes_changed(heatfloor_modes_message);
			heatfloor_set_on_program_changed(heatfloor_program_message);
		
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;
		
	wdt_enable(WDTO_2S);
	while (1){
		if (!clunet_buffered_is_empty()){
			cmd(clunet_buffered_pop());
		}
		
		if (systime != cur_time){
			cur_time = systime;
			
			heatfloor_tick_second();
			DS18B20_TickSecondForCache();
		}
		wdt_reset();
	}
	return 0;
}