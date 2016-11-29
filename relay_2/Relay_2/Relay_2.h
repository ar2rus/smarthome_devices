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
#include "clunet/clunet_buffered.h"

#include "fan/fan.h"

#include "timer/timer.h"

#include <avr/interrupt.h>
#include <util/delay.h>


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
#define LIGHT_SENSOR_DEVICE_ID 0x1E

#define TOOTHBRUSH_RELAY_ID RELAY_0_ID
#define FAN_RELAY_ID RELAY_1_ID
#define MIRRORED_BOX_LIGHT_RELAY_ID RELAY_2_ID

#define TOOTHBRUSH_RELAY_STATE RELAY_0_STATE



/* main timer controls*/
#define TIMER_PRESCALER 1024
#define TIMER_NUM_TICKS (unsigned int)(1 * F_CPU / TIMER_PRESCALER) + 100	/*1 second main loop*/
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit2(TCCR1B, CS12, CS10); unset_bit(TCCR1B, CS11); /*1024x prescaler*/}

#define TIMER_REG TCNT1

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define TIMER_COMP_VECTOR TIMER1_COMPA_vect


void start_charge(unsigned int num_seconds, unsigned char schedule);



#endif /* RELAY_2_H_ */