/*
 * heatfloor_config.h
 *
 * Created: 12.10.2015 15:08:43
 *  Author: gargon
 */ 


#ifndef HEATFLOOR_CONFIG_H_
#define HEATFLOOR_CONFIG_H_

//number of active channel -> for memory buffer allocating
#define HEATFLOOR_CHANNELS_COUNT 2

//max number of different dispatcher programs -> for eeprom allocating
#define HEATFLOOR_PROGRAMS_COUNT 10

//in seconds
#define HEATFLOOR_SENSOR_CHECK_TIME 30

//in degrees, Celsius
#define HEATFLOOR_SENSOR_HYSTERESIS_TEMPERATURE 0.3


#define EEPROM_ADDRESS_HEATFLOOR 0x00

#endif /* HEATFLOOR_CONFIG_H_ */