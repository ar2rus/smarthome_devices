/*
 * Relay_2.h
 *
 * Created: 15.06.2015
 *  Author: gargon
 */ 


#ifndef RELAY_2_H_
#define RELAY_2_H_


#include "utils/bits.h"
#include "clunet/clunet.h"

#include "fan/fan.h"



/*relay_0 description*/
#define RELAY_0_ID 1
#define RELAY_0_PORT D
#define RELAY_0_PIN  5

/*relay_1 description*/
#define RELAY_1_ID 2
#define RELAY_1_PORT D
#define RELAY_1_PIN  6

/*relay_2 description*/
#define RELAY_2_ID 3
#define RELAY_2_PORT D
#define RELAY_2_PIN  7


/*relay_0 controls*/
#define RELAY_0_INIT set_bit(DDRPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_ON set_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_OFF unset_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_TOGGLE flip_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_STATE bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)


/*relay_1 controls*/
#define RELAY_1_INIT set_bit(DDRPORT(RELAY_1_PORT), RELAY_1_PIN)
#define RELAY_1_ON set_bit(OUTPORT(RELAY_1_PORT), RELAY_1_PIN)
#define RELAY_1_OFF unset_bit(OUTPORT(RELAY_1_PORT), RELAY_1_PIN)
#define RELAY_1_TOGGLE flip_bit(OUTPORT(RELAY_1_PORT), RELAY_1_PIN)
#define RELAY_1_STATE bit(OUTPORT(RELAY_1_PORT), RELAY_1_PIN)

/*relay_2 controls*/
#define RELAY_2_INIT set_bit(DDRPORT(RELAY_2_PORT), RELAY_2_PIN)
#define RELAY_2_ON set_bit(OUTPORT(RELAY_2_PORT), RELAY_2_PIN)
#define RELAY_2_OFF unset_bit(OUTPORT(RELAY_2_PORT), RELAY_2_PIN)
#define RELAY_2_TOGGLE flip_bit(OUTPORT(RELAY_2_PORT), RELAY_2_PIN)
#define RELAY_2_STATE bit(OUTPORT(RELAY_2_PORT), RELAY_2_PIN)


#define HUMIDITY_SENSOR_DEVICE_ID 0x1E
#define DOORS_MIRRORED_BOX_DEVICE_ID 0x1E

#define FAN_RELAY_ID RELAY_1_ID
#define MIRRORED_BOX_LIGHT_RELAY_ID RELAY_2_ID



#endif /* RELAY_2_H_ */