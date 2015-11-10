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
*   Returns :       коды - READ_CRC_ERROR, если считанные данные не прошли проверку
*                          READ_SUCCESSFUL, если данные прошли проверку    
*   Parameters :    bus - вывод микроконтроллера, который выполн€ет роль 1WIRE шины
*                   *id - им€ массива из 8-ми элементов, в котором хранитс€
*                         адрес датчика DS18B20
*                   *temperature - указатель на шестнадцати разр€дную переменную
*                                в которой будет сохранено считанного зн. температуры
*   Purpose :      јдресует датчик DS18B20, дает команду на преобразование температуры
*                  ждет, считывает его пам€ть - scratchpad, провер€ет CRC,
*                  сохран€ет значение температуры в переменной, возвращает код ошибки             
*****************************************************************************/
unsigned char DS18B20_ReadTemperature(unsigned char bus, unsigned char* id, signed int* temperature)
{
    unsigned char scratchpad[9];
    unsigned char i;
  
    /*подаем сигнал сброса
    команду дл€ адресации устройства на шине
    подаем команду - запук преобразовани€ */
    OWI_DetectPresence(bus);
    OWI_MatchRom(id, bus);
    OWI_SendByte(DS18B20_CONVERT_T, bus);

    /*ждем, когда датчик завершит преобразование*/ 
    while (!OWI_ReadBit(bus));

    /*подаем сигнал сброса
    команду дл€ адресации устройства на шине
    команду - чтение внутренней пам€ти
    затем считываем внутреннюю пам€ть датчика в массив
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
// *   Returns :         нет       
// *   Parameters :     temperature - температура датчика DS18B20     
// *   Purpose :        ¬ыводит значение температуры датчика DS18B20
// *                    на LCD. јдрес знакоместа нужно выставл€ть заранее.
// *****************************************************************************/
// void DS18B20_PrintTemperature(unsigned int temperature)
// {
//   unsigned char tmp = 0;
//   /*выводим знак температуры
//   *если она отрицательна€ 
//   *делаем преобразование*/  
//   if ((temperature & 0x8000) == 0){
//     LCD_WriteData('+');
//   }
//   else{
//     LCD_WriteData('-');
//     temperature = ~temperature + 1;
//   }
//         
//   //выводим значение целое знач. температуры      
//   tmp = (unsigned char)(temperature>>4);
//   if (tmp<100){
//     BCD_2Lcd(tmp);
//   }
//   else{
//     BCD_3Lcd(tmp);    
//   }
//         
//   //выводим дробную часть знач. температуры
//   tmp = (unsigned char)(temperature&15);
//   tmp = (tmp>>1) + (tmp>>3);
//   LCD_WriteData('.');
//   BCD_1Lcd(tmp);
// }