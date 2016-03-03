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
*   Parameters :    bus - ����� ����������������, ������� ��������� ���� 1WIRE ����
*                   *id - ��� ������� �� 8-�� ���������, � ������� ��������
*                         ����� ������� DS18B20
*                   accuracy - �������� �������� ����������� ��� ������������
*						0	-	9bit
*						1	-	10bit
*						2	-	11bit
*						3	-	12bit
*					
*   Purpose :      �������� ������ DS18B20, ���������� � ������ (scratchpad),
*				   �������� scratchpad � EEPROM. 
*				   ������� �������� ������ ���� ��� ��� ��������� ����������
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
	
	/*����, ����� ������ � EEPROM ����������*/
	while (!OWI_ReadBit(bus));
	//while (!(OWI_PIN & bus));		//without sei(), cli();
}


/*****************************************************************************
*   Function name : DS18B20_ReadDevice
*   Returns :       ���� - READ_CRC_ERROR, ���� ��������� ������ �� ������ ��������
*                          READ_SUCCESSFUL, ���� ������ ������ ��������
*   Parameters :    bus - ����� ����������������, ������� ��������� ���� 1WIRE ����
*                   *id - ��� ������� �� 8-�� ���������, � ������� ��������
*                         ����� ������� DS18B20
*                   *temperature - ��������� �� ����������� ��������� ����������
*                                � ������� ����� ��������� ���������� ��. �����������
*   Purpose :      ����� ������ ��������� �������� ��� ���������� ����������� �� scratchpad,
*				   �� ��������� ���������
*				   �������� ������ DS18B20, ��������� ��� ������ - scratchpad, ��������� CRC,
*                  ��������� �������� ����������� � ����������, ���������� ��� ������
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
*   Parameters :    bus - ����� ����������������, ������� ��������� ���� 1WIRE ����
*   Purpose :      ��������� ��������� �� ���� ����������� ������������,
*                  ���� ��������� ��������������
*****************************************************************************/
void DS18B20_StartAllDevicesConverting(unsigned char bus){
    OWI_DetectPresence(bus);
    OWI_SkipRom(bus);
    OWI_SendByte(DS18B20_CONVERT_T, bus);

    /*����, ����� ������ �������� ��������������*/ 
    //while (!OWI_ReadBit(bus));
	while (!(OWI_PIN & bus));		//without sei(), cli();
}

/*****************************************************************************
*   Function name :   DS18B20_StartDeviceConvertingAndRead
*   Returns :       ���� - READ_CRC_ERROR, ���� ��������� ������ �� ������ ��������
*                          READ_SUCCESSFUL, ���� ������ ������ ��������    
*   Parameters :    bus - ����� ����������������, ������� ��������� ���� 1WIRE ����
*                   *id - ��� ������� �� 8-�� ���������, � ������� ��������
*                         ����� ������� DS18B20
*                   *temperature - ��������� �� ����������� ��������� ����������
*                                � ������� ����� ��������� ���������� ��. �����������
*   Purpose :      ��������� ��������� � ���������� �������� �����������
*				   �������� ������ DS18B20, ���� ������� �� �������������� �����������
*                  ����, ��������� ��� ������ - scratchpad, ��������� CRC,
*                  ��������� �������� ����������� � ����������, ���������� ��� ������             
*****************************************************************************/
unsigned char DS18B20_StartDeviceConvertingAndRead(unsigned char bus, unsigned char* id, signed int* temperature){
    OWI_DetectPresence(bus);
    OWI_MatchRom(id, bus);
    OWI_SendByte(DS18B20_CONVERT_T, bus);

    /*����, ����� ������ �������� ��������������*/ 
    //while (!OWI_ReadBit(bus));
	while (!(OWI_PIN & bus));		//without sei(), cli();

   return DS18B20_ReadDevice(bus, id, temperature);
}