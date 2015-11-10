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
*   Function name :   DS18B20_ReadTemperature
*   Returns :       ���� - READ_CRC_ERROR, ���� ��������� ������ �� ������ ��������
*                          READ_SUCCESSFUL, ���� ������ ������ ��������    
*   Parameters :    bus - ����� ����������������, ������� ��������� ���� 1WIRE ����
*                   *id - ��� ������� �� 8-�� ���������, � ������� ��������
*                         ����� ������� DS18B20
*                   *temperature - ��������� �� ����������� ��������� ����������
*                                � ������� ����� ��������� ���������� ��. �����������
*   Purpose :      �������� ������ DS18B20, ���� ������� �� �������������� �����������
*                  ����, ��������� ��� ������ - scratchpad, ��������� CRC,
*                  ��������� �������� ����������� � ����������, ���������� ��� ������             
*****************************************************************************/
unsigned char DS18B20_ReadTemperature(unsigned char bus, unsigned char* id, signed int* temperature)
{
    unsigned char scratchpad[9];
    unsigned char i;
  
    /*������ ������ ������
    ������� ��� ��������� ���������� �� ����
    ������ ������� - ����� �������������� */
    OWI_DetectPresence(bus);
    OWI_MatchRom(id, bus);
    OWI_SendByte(DS18B20_CONVERT_T, bus);

    /*����, ����� ������ �������� ��������������*/ 
    while (!OWI_ReadBit(bus));

    /*������ ������ ������
    ������� ��� ��������� ���������� �� ����
    ������� - ������ ���������� ������
    ����� ��������� ���������� ������ ������� � ������
    */
    OWI_DetectPresence(bus);
    OWI_MatchRom(id, bus);
    OWI_SendByte(DS18B20_READ_SCRATCHPAD, bus);
    for (i = 0; i<=8; i++){
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
	
	*temperature *= 0.625f;
	
   // *temperature = (unsigned int)scratchpad[0];
   // *temperature |= ((unsigned int)scratchpad[1] << 8);
    
    return READ_SUCCESSFUL;
}

// /*****************************************************************************
// *   Function name :  DS18B20_PrintTemperature 
// *   Returns :         ���       
// *   Parameters :     temperature - ����������� ������� DS18B20     
// *   Purpose :        ������� �������� ����������� ������� DS18B20
// *                    �� LCD. ����� ���������� ����� ���������� �������.
// *****************************************************************************/
// void DS18B20_PrintTemperature(unsigned int temperature)
// {
//   unsigned char tmp = 0;
//   /*������� ���� �����������
//   *���� ��� ������������� 
//   *������ ��������������*/  
//   if ((temperature & 0x8000) == 0){
//     LCD_WriteData('+');
//   }
//   else{
//     LCD_WriteData('-');
//     temperature = ~temperature + 1;
//   }
//         
//   //������� �������� ����� ����. �����������      
//   tmp = (unsigned char)(temperature>>4);
//   if (tmp<100){
//     BCD_2Lcd(tmp);
//   }
//   else{
//     BCD_3Lcd(tmp);    
//   }
//         
//   //������� ������� ����� ����. �����������
//   tmp = (unsigned char)(temperature&15);
//   tmp = (tmp>>1) + (tmp>>3);
//   LCD_WriteData('.');
//   BCD_1Lcd(tmp);
// }