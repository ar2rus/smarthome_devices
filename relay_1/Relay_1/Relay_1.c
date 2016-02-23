
#include "Relay_1.h"

#include <avr/interrupt.h>
#include <util/delay.h>

OWI_device devices[OWI_MAX_BUS_DEVICES];

signed char switchState(char id){
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

/*
*	Инициализирует поиск устройств на шине 1-Wire
*	и возвращает кол-во найденных устройств
*	или -1 в случае ошибки
*/
char oneWireSearch(OWI_device* devices){
	unsigned char num = 0;
	if (OWI_SearchDevices(devices, OWI_MAX_BUS_DEVICES, OWI_BUS, &num) == SEARCH_CRC_ERROR){
		return -1;
	}else{
		return num;
	}
}

/*
*  Запрос температуры датчика по его описателю OWI_device
*  Возвращает 1 если запрос выполнен и 0 - если нет
*/
char temperatureRequest(OWI_device* device, signed int* temperature){
	if (DS18B20_StartDeviceConvertingAndRead(OWI_BUS, (*device).id, temperature) != READ_CRC_ERROR){
		return 1;
	}
	return 0;
}

/*
* Запрос температуры для устройств devices одновременно
*/
void temperatureResponse(unsigned char address, OWI_device* devices, unsigned char size){
	char temperatureInfo[11 * size + 1];
	unsigned char cnt = 0;
	
	if (size > 0){
		DS18B20_StartAllDevicesConverting(OWI_BUS);
		for (char i=0; i<size; i++){
			unsigned char pos = 1 + 11*cnt;
			if (DS18B20_ReadDevice(OWI_BUS, (*devices).id, (signed int *)&temperatureInfo[pos + 9]) != READ_CRC_ERROR){
				temperatureInfo[pos] = 0; //1-wire
				memcpy(&temperatureInfo[pos + 1], (*devices).id, sizeof((*devices).id));
				cnt++;
			}
			devices++;
		}
	}

	temperatureInfo[0] = cnt;
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_TEMPERATURE_INFO, ((char*)temperatureInfo), 11 * cnt + 1);
}

void heatfloor_state_response(unsigned char address, heatfloor_channel_infos* infos){
	clunet_send_fairy(address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_HEATFLOOR_INFO, ((char*)infos), infos->num * sizeof(heatfloor_channel_info) + 1);
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	switch(command){
		case CLUNET_COMMAND_SWITCH:
		if (data[0] == 0xFF){	//info request
			if (size == 1){
				switchResponse(src_address);
			}
		}else{
			if (size == 2){
				switch(data[0]){
					case 0x00:
					case 0x01:
					case 0x02:
						switchExecute(data[1], data[0]);
					break;
					case 0x03:
					for (char i=0; i<8; i++){
						switchExecute(i+1, bit(data[1], i));
					}
					break;
				}
				switchResponse(CLUNET_BROADCAST_ADDRESS);
			}
		}
		break;
		case CLUNET_COMMAND_ONEWIRE_SEARCH:
		if (size == 0){
			
			char num = oneWireSearch(devices);
			if (num >=0){
				char oneWireDevices[sizeof((*devices).id) * num + 1];
			
				oneWireDevices[0] = num;
				for (unsigned char i=0; i<num; i++){
					memcpy(&oneWireDevices[i * sizeof((*devices).id) + 1], devices[i].id, sizeof((*devices).id));
				}
				clunet_send_fairy(src_address, CLUNET_PRIORITY_INFO, CLUNET_COMMAND_ONEWIRE_INFO, (char*)&oneWireDevices, sizeof(oneWireDevices));
			}
		}
		break;
		case CLUNET_COMMAND_TEMPERATURE:
		if (size >= 1){
			switch(data[0]){
				case 1:	//1-wire термометры
				//проверяем, что запрашиваются все 1-wire термометры
				//и если запрашиваются они - то выполняем как для data[0] = 0
				if (size != 2 || data[1] != 0){
					break;
				}
				case 0:	{//все устройства
					char num = oneWireSearch(devices);
					temperatureResponse(src_address, devices, num);
				}
				break;
				case 2:	//запрос по серийнику
				if (size == 10 && data[1] == 0){	//запрос по 1-wire
					temperatureResponse(src_address, (OWI_device*)&data[2], 1);
				}
				break;
			}
		}
		break;
		case CLUNET_COMMAND_DOOR_INFO:
			if (src_address == DOORS_SENSOR_DEVICE_ID){
				if (size==1 && data[0] >=0){
					unsigned char doors_opened = data[0]>0;
					signed char state = switchState(WARDROBE_LIGHT_RELAY_ID);
					if (state >=0 && state != doors_opened){
						switchExecute(WARDROBE_LIGHT_RELAY_ID, doors_opened);
						switchResponse(CLUNET_BROADCAST_ADDRESS);
					}
				}
			}
		break;
		case CLUNET_COMMAND_HEATFLOOR:
			switch (size){
				case 0x01:
					switch(data[0]){
						case 0xFF:
							heatfloor_state_response(src_address, heatfloor_refresh());
							break;
						//case 0xFE:	//setup ds18b20 (temporary)
						//	DS18B20_SetDeviceAccuracy(OWI_BUS, &HEATING_FLOOR_CHANNEL_0_SENSOR_1W_ID, 3);
						//	break;
					}
					break;
				case 0x02:
					switch(data[0]){
						case 0x00:
						case 0x01:
						heatfloor_enable(data[1] & 0x7, data[0]);
						break;
					}
					break;
			}
		break;
		case CLUNET_COMMAND_TIME:
			//heatfloor_set_systime();
		break;
	}
}

signed int heatfloor_sensor_temperature_request(unsigned char channel){
	signed int t;
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
		if (temperatureRequest(device, &t)){
			return t;
		}
	}
	return -1;
}

char heatfloor_switch_exec(unsigned char channel, unsigned char on_){
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
			
			return 1;
		}
	}
	
	return -1;
}

void heatfloor_state_message(heatfloor_channel_infos* infos){
	heatfloor_state_response(CLUNET_BROADCAST_ADDRESS, infos);
}

void heatfloor_systime_request(){
	clunet_send_fairy(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_TIME, 0, 0);
}

int main(void){
	
	cli();
	
	RELAY_0_INIT;
	RELAY_1_INIT;
	RELAY_2_INIT;
	
	OWI_Init(OWI_BUS);
	
	heatfloor_init(
		heatfloor_sensor_temperature_request,
		heatfloor_switch_exec, 
		heatfloor_systime_request
		);
		
	heatfloor_set_on_state_message(heatfloor_state_message);
	
	
	clunet_set_on_data_received(clunet_data_received);
	clunet_init();
	
	//heatfloor_enable(HEATING_FLOOR_CHANNEL_KITCHEN, 1);
	//heatfloor_enable(HEATING_FLOOR_CHANNEL_BATHROOM, 1);
	
	//sei();
	
	while (1){
	}
	return 0;
}