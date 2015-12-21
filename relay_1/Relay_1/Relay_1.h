/*
 * Relay_1.h
 *
 * Created: 10.05.2015 14:12:34
 *  Author: gargon
 */ 


#ifndef RELAY_1_H_
#define RELAY_1_H_

#include "utils/bits.h"
#include "clunet/clunet.h"

#include "onewire/OWIBitFunctions.h"
#include "onewire/OWIHighLevelFunctions.h"
#include "onewire/ds18b20.h"

#include "heatfloor/heatfloor.h"



/*relay_0 description*/
#define RELAY_0_ID 1
#define RELAY_0_PORT D
#define RELAY_0_PIN  5

/*relay_1 description*/
#define RELAY_1_ID 2
#define RELAY_1_PORT B
#define RELAY_1_PIN  7

/*relay_2 description*/
#define RELAY_2_ID 3
#define RELAY_2_PORT B
#define RELAY_2_PIN  6


/*relay_0 controls*/
#define RELAY_0_INIT set_bit(DDRPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_ON set_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_OFF unset_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_TOGGLE flip_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_STATE bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)

/*relay_1 controls*/
#define RELAY_1_INIT set_bit(DDRPORT(RELAY_1_PORT), RELAY_1_PIN)
#define RELAY_1_ON   set_bit(OUTPORT(RELAY_1_PORT), RELAY_1_PIN)
#define RELAY_1_OFF  unset_bit(OUTPORT(RELAY_1_PORT), RELAY_1_PIN)
#define RELAY_1_TOGGLE  flip_bit(OUTPORT(RELAY_1_PORT), RELAY_1_PIN)
#define RELAY_1_STATE bit(OUTPORT(RELAY_1_PORT), RELAY_1_PIN)

/*relay_2 controls*/
#define RELAY_2_INIT set_bit(DDRPORT(RELAY_2_PORT), RELAY_2_PIN)
#define RELAY_2_ON   set_bit(OUTPORT(RELAY_2_PORT), RELAY_2_PIN)
#define RELAY_2_OFF  unset_bit(OUTPORT(RELAY_2_PORT), RELAY_2_PIN)
#define RELAY_2_TOGGLE  flip_bit(OUTPORT(RELAY_2_PORT), RELAY_2_PIN)
#define RELAY_2_STATE bit(OUTPORT(RELAY_2_PORT), RELAY_2_PIN)

#define DOORS_SENSOR_DEVICE_ID 0x1F
#define WARDROBE_LIGHT_RELAY_ID RELAY_0_ID


//bathroom heating floor
#define HEATING_FLOOR_CHANNEL_0 0
//alias
#define HEATING_FLOOR_CHANNEL_BATHROOM HEATING_FLOOR_CHANNEL_0
#define HEATING_FLOOR_CHANNEL_0_RELAY_ID RELAY_2_ID
const char HEATING_FLOOR_CHANNEL_0_SENSOR_1W_ID[] = {0x28, 0xFF, 0x9A, 0x51, 0x52, 0x15, 0x01, 0x6C};
	
//kitchen heating floor
#define HEATING_FLOOR_CHANNEL_1 1
//alias
#define HEATING_FLOOR_CHANNEL_KITCHEN HEATING_FLOOR_CHANNEL_1
#define HEATING_FLOOR_CHANNEL_1_RELAY_ID RELAY_1_ID
const char HEATING_FLOOR_CHANNEL_1_SENSOR_1W_ID[] = {0x28, 0xFF, 0xAB, 0x6E, 0x4C, 0x04, 0x00, 0x3F};
	

#endif /* RELAY_1_H_ */