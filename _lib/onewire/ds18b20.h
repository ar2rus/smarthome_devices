/*
 * ds18b20.h
 *
 * Created: 12.05.2015 11:41:55
 *  Author: gargon
 */ 


#ifndef DS18B20_H_
#define DS18B20_H_


//коды результата для функции чтения температуры
#define READ_SUCCESSFUL   0x00
#define READ_CRC_ERROR    0x01

//код семейства и коды команд датчика DS18B20
#define DS18B20_FAMILY_ID                0x28
#define DS18B20_CONVERT_T                0x44
#define DS18B20_READ_SCRATCHPAD          0xbe
#define DS18B20_WRITE_SCRATCHPAD         0x4e
#define DS18B20_COPY_SCRATCHPAD          0x48
#define DS18B20_RECALL_E                 0xb8
#define DS18B20_READ_POWER_SUPPLY        0xb4

void DS18B20_SetDeviceAccuracy(unsigned char bus, unsigned char* id, unsigned char accuracy);

void DS18B20_StartAllDevicesConverting(unsigned char bus);
unsigned char DS18B20_ReadDevice(unsigned char bus, unsigned char* id, signed int* temperature);
unsigned char DS18B20_StartDeviceConvertingAndRead(unsigned char bus, unsigned char* id, signed int* temperature);

#endif /* DS18B20_H_ */