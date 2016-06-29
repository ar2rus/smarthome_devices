/*
 * lc75341_config.h
 *
 * Created: 22.10.2014 20:17:13
 *  Author: gargon
 */ 

#ifndef LC75341_CONFIG_H_
#define LC75341_CONFIG_H_


/*LC75341 port description*/
#define LC75341_DO_PORT C
#define LC75341_DO_PIN  0

#define LC75341_CE_PORT C
#define LC75341_CE_PIN  1

#define LC75341_CL_PORT B
#define LC75341_CL_PIN  5


//just comment this address - not to use saving/restoring params from eeprom
#define LC75341_EEPROM_ADDRESS 0x01

#endif /* LC75341_CONFIG_H_ */