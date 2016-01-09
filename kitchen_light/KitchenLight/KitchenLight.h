/*
 * Wardrobe.h
 *
 * Created: 10.06.2015
 *  Author: gargon
 */ 


#ifndef KitchenLight_H_
#define KitchenLight_H_

#include "utils/bits.h"
#include "clunet/clunet.h"


#define BUTTON_ID 2

#define BUTTON_PORT C
#define BUTTON_PIN 4

#define BUTTON_INIT {unset_bit(DDRPORT(BUTTON_PORT), BUTTON_PIN); set_bit(OUTPORT(BUTTON_PORT), BUTTON_PIN);}
#define BUTTON_READ (!bit(INPORT(BUTTON_PORT), BUTTON_PIN))

/*relay_0 description*/
#define RELAY_0_ID 1
#define RELAY_0_PORT C
#define RELAY_0_PIN  1

/*relay_0 controls*/
#define RELAY_0_INIT set_bit(DDRPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_ON set_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_OFF unset_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_TOGGLE flip_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_STATE bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)



#endif /* KitchenLight_H_ */