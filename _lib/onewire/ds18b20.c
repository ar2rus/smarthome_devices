/*
 * ds18b20.c
 *
 * Created: 12.05.2015 11:43:17
 *  Author: gargon
 */ 


#include "ds18b20.h"

#include "OWIBitFunctions.h"
#include "OWIHighLevelFunctions.h"
#include "OWIcrc.h"

#include <util/delay.h>

/*****************************************************************************
*   Function name : DS18B20_SetDeviceAccuracy
*   Parameters :    bus - вывод микроконтроллера, который выполняет роль 1WIRE шины
*                   *id - имя массива из 8-ми элементов, в котором хранится
*                         адрес датчика DS18B20
*                   accuracy - значение точность необходимой для установления
*						0	-	9bit
*						1	-	10bit
*						2	-	11bit
*						3	-	12bit
*					
*   Purpose :      Адресует датчик DS18B20, записывает в память (scratchpad),
*				   копирует scratchpad в EEPROM. 
*				   Следует вызывать только один раз для настройки устройства
*****************************************************************************/
void DS18B20_SetDeviceAccuracy(unsigned char bus, unsigned char* id, unsigned char accuracy){
	OWI_DetectPresence(bus);
	OWI_MatchRom(id, bus);
	OWI_SendByte(DS18B20_WRITE_SCRATCHPAD, bus);

	OWI_SendByte(0x00, bus);	//Th
	OWI_SendByte(0x00, bus);	//Tl
	OWI_SendByte(0x1F | ((accuracy & 0x03)<<5), bus);	//Config
	
	OWI_DetectPresence(bus);
	OWI_MatchRom(id, bus);
	OWI_SendByte(DS18B20_COPY_SCRATCHPAD, bus);
	
	/*ждем, когда запись в EEPROM завершится*/
	while (!OWI_ReadBit(bus));
	//while (!(OWI_PIN & bus));		//without sei(), cli();
}


/*****************************************************************************
*   Function name : DS18B20_ReadDevice
*   Returns :       коды - READ_CRC_ERROR, если считанные данные не прошли проверку
*                          READ_SUCCESSFUL, если данные прошли проверку
*   Parameters :    bus - вывод микроконтроллера, который выполняет роль 1WIRE шины
*                   *id - имя массива из 8-ми элементов, в котором хранится
*                         адрес датчика DS18B20
*                   *temperature - указатель на шестнадцати разрядную переменную
*                                в которой будет сохранено считанного зн. температуры
*   Purpose :      Метод только считывает значение УЖЕ ИЗМЕРЕННОЙ температуры из scratchpad,
*				   НЕ ВЫПОЛНЯЕТ ИЗМЕРЕНИЕ
*				   Адресует датчик DS18B20, считывает его память - scratchpad, проверяет CRC,
*                  сохраняет значение температуры в переменной, возвращает код ошибки
*****************************************************************************/
unsigned char DS18B20_ReadDevice(unsigned char bus, unsigned char* id, signed int* temperature){
	
	unsigned char scratchpad[9];
	
	OWI_DetectPresence(bus);
	OWI_MatchRom(id, bus);
	OWI_SendByte(DS18B20_READ_SCRATCHPAD, bus);
	for (unsigned char i = 0; i <= 8; i++){
		scratchpad[i] = OWI_ReceiveByte(bus);
	}
	
	if(OWI_CheckScratchPadCRC(scratchpad) != OWI_CRC_OK){
		return READ_CRC_ERROR;
	}
	
	*temperature = (unsigned int)scratchpad[0];
	*temperature |= ((unsigned int)scratchpad[1] << 8);
	
	if ((*temperature & 0x8000) != 0){
		*temperature = -(~(*temperature) + 1);
	}
	
	//*temperature *= 0.625f;
	
	*temperature *= 5;	//0.625 = 5/8
	*temperature /= 8;
	
	return READ_SUCCESSFUL;
}


/*****************************************************************************
*   Function name :   DS18B20_StartAllDevicesConverting
*   Parameters :    bus - вывод микроконтроллера, который выполняет роль 1WIRE шины
*   Purpose :      Запускает измерение на всех устройствах одновременно,
*                  ждет окончания преобразования
*****************************************************************************/
void DS18B20_StartAllDevicesConverting(unsigned char bus){
    OWI_DetectPresence(bus);
    OWI_SkipRom(bus);
    OWI_SendByte(DS18B20_CONVERT_T, bus);

    /*ждем, когда датчик завершит преобразование*/ 
    //while (!OWI_ReadBit(bus));
	while (!(OWI_PIN & bus));		//without sei(), cli();
}

/*****************************************************************************
*   Function name :   DS18B20_StartDeviceConvertingAndRead
*   Returns :       коды - READ_CRC_ERROR, если считанные данные не прошли проверку
*                          READ_SUCCESSFUL, если данные прошли проверку    
*   Parameters :    bus - вывод микроконтроллера, который выполняет роль 1WIRE шины
*                   *id - имя массива из 8-ми элементов, в котором хранится
*                         адрес датчика DS18B20
*                   *temperature - указатель на шестнадцати разрядную переменную
*                                в которой будет сохранено считанного зн. температуры
*   Purpose :      ВЫПОЛНЯЕТ ИЗМЕРЕНИЕ И ВОЗВРАЩАЕТ ЗНАЧЕНИЕ ТЕМПЕРАТУРЫ
*				   Адресует датчик DS18B20, дает команду на преобразование температуры
*                  ждет, считывает его память - scratchpad, проверяет CRC,
*                  сохраняет значение температуры в переменной, возвращает код ошибки             
*****************************************************************************/
unsigned char DS18B20_StartDeviceConvertingAndRead(unsigned char bus, unsigned char* id, signed int* temperature){
    OWI_DetectPresence(bus);
    OWI_MatchRom(id, bus);
    OWI_SendByte(DS18B20_CONVERT_T, bus);

    /*ждем, когда датчик завершит преобразование*/ 
    //while (!OWI_ReadBit(bus));
	while (!(OWI_PIN & bus));		//without sei(), cli();

   return DS18B20_ReadDevice(bus, id, temperature);
}