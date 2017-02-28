/*
 * bht1750.c
 *
 * Created: 08.01.2017 17:57:28
 *  Author: gargon
 */ 

#include <avr/io.h>

#include "../USI_TWI/USI_TWI_Master.h"
#include "bht1750.h"


uint8_t bht1750_start_mtreg(uint8_t mode, uint8_t mtreg){
	uint8_t ret = 0;
	
	char data[2];
	data[0] = (BH1750_I2CADDR << 1) | I2C_WRITE;
	if (mtreg){
		USI_TWI_Master_Start();
		data[1] = 0x40 | ((mtreg & 0xE0)>>5);
		USI_TWI_Master_SendData((char*)&data, sizeof(data));
		USI_TWI_Master_Stop();	
		
		USI_TWI_Master_Start();
		data[1] = 0x60 | (mtreg & 0x1F); 
		USI_TWI_Master_SendData((char*)&data, sizeof(data));
		USI_TWI_Master_Stop();
	}
	
	USI_TWI_Master_Start();
	data[1] = mode;
	if (USI_TWI_Master_SendData((char*)&data, sizeof(data))){
		ret = 1;
	}
	USI_TWI_Master_Stop();
	return ret;
}

uint8_t bht1750_start(uint8_t mode){
	return bht1750_start_mtreg(mode, 0);
}

uint8_t bht1750_readValues(char* values){
	uint8_t ret = 0;
	USI_TWI_Master_Start();
	char data = (BH1750_I2CADDR << 1) | I2C_READ;
	if (USI_TWI_Master_SendData(&data, 1)){
		if (USI_TWI_Master_ReadData(values, 2)){
			ret = 2;
		}
	}
	
	USI_TWI_Master_Stop();
	return ret;
}