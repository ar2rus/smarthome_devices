/*
 * bme280.c
 *
 * Created: 08.01.2017 12:34:06
 *  Author: gargon
 */ 

#include <avr/io.h>
#include <util/delay.h>

#include "../USI_TWI/USI_TWI_Master.h"
#include "bme280.h"


uint8_t readRegisters(uint8_t offset_to_read, char* data_to_read, uint8_t size_to_read){
	uint8_t ret = 0;
	
	USI_TWI_Master_Start();
	char data[2];
	data[0] = (BME280_I2C_ADDRESS << 1) | I2C_WRITE;
	data[1] = offset_to_read;
	if (USI_TWI_Master_SendData(data, sizeof(data))){
		//restart
		USI_TWI_Master_Start();
		data[0] = (BME280_I2C_ADDRESS << 1) | I2C_READ;
		if (USI_TWI_Master_SendData(data, 1)){
			if (USI_TWI_Master_ReadData(data_to_read, size_to_read)){
				ret = size_to_read;
			}
		}
	}
	
	USI_TWI_Master_Stop();
 	
 	return ret;
}

uint8_t writeRegister(uint8_t offset_to_write, uint8_t data_to_write){
	uint8_t ret = 0;
	USI_TWI_Master_Start();
	char data[3];
	data[0] = (BME280_I2C_ADDRESS << 1) | I2C_WRITE;
	data[1] = offset_to_write;
	data[2] = data_to_write;
	if (USI_TWI_Master_SendData(data, sizeof(data))){
		ret = 1;
	}
	USI_TWI_Master_Stop();
 	return ret;
}

void bme280_start_force(/*uint8_t filter, uint8_t stdby, */uint8_t h_oversample, uint8_t t_oversample, uint8_t p_oversample){

//Set the oversampling control words.
//config will only be writable in sleep mode, so first insure that.
writeRegister(BME280_CTRL_MEAS_REG, 0x00);
uint8_t dataToWrite = 0;
//Set the config word
//dataToWrite = (stdby << 0x5) & 0xE0;	//standby for normal mode off
//dataToWrite |= (filter << 0x02) & 0x1C;	//filter off
//writeRegister(BME280_CONFIG_REG, dataToWrite);

//Set ctrl_hum first, then ctrl_meas to activate ctrl_hum
dataToWrite = h_oversample & 0x07; //1 oversample for humidity, all other bits can be ignored
writeRegister(BME280_CTRL_HUMIDITY_REG, dataToWrite);

//set ctrl_meas
//First, set temp oversampling
dataToWrite = (t_oversample << 0x5) & 0xE0;	//1 oversample for temp
//Next, pressure oversampling
dataToWrite |= (p_oversample << 0x02) & 0x1C;	//1 oversample for pressure
//Last, set mode
dataToWrite |= (/*mode*/BME280_MODE_FORCE) & 0x03;
//Load the byte
writeRegister(BME280_CTRL_MEAS_REG, dataToWrite);

//if (readRegisters(BME280_CHIP_ID_REG, &dataToWrite, 1)){
//	return dataToWrite;
//}
//return 0;
}

uint8_t bme280_readValues(char* values){
	
// 	//wait for conversion
// 	char status;
// 	do{
// 		_delay_us(1000);
// 		if (!readRegisters(BME280_STAT_REG, &status, 1)){
// 			return 0;
// 		}
// 	}while (status & 0x08);
	
	if (readRegisters(BME280_PRESSURE_MSB_REG, values, 8)){
		return 8;
	}
	return 0;
}