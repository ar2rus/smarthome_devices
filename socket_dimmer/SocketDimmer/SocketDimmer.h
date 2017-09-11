/*
 * Relay_1.h
 *
 * Created: 10.05.2015 14:12:34
 *  Author: gargon
 */ 


#ifndef SOCKET_DIMMER
#define SOCKET_DIMMER

#include "utils/bits.h"

#include "clunet/clunet.h"
#include "clunet/clunet_buffered.h"


#include <avr/interrupt.h>
#include <util/delay.h>



/* main timer controls*/
#define TIMER_PRESCALER 64
#define TIMER_COUNTER TCNT1
#define TIMER_NUM_TICKS (unsigned int)(40e-6 * F_CPU / TIMER_PRESCALER)	/*40mks PWM tick*/
#define TIMER_INIT {TCCR1B = 0; TIMER_COUNTER = 0; OCR1A = TIMER_NUM_TICKS; set_bit2(TCCR1B, CS11, CS10); unset_bit(TCCR1B, CS12); /*64x prescaler*/}

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define TIMER_COMP_VECTOR TIMER1_COMPA_vect


/*AC zero detection*/
#define ZERO_DETECTOR_PORT D
#define ZERO_DETECTOR_PIN 2
#define ZERO_DETECTOR_INIT {unset_bit(DDRPORT(ZERO_DETECTOR_PORT), ZERO_DETECTOR_PIN); set_bit(OUTPORT(ZERO_DETECTOR_PORT), ZERO_DETECTOR_PIN);}
#define ZERO_DETECTOR_READ (!bit(INPORT(ZERO_DETECTOR_PORT), ZERO_DETECTOR_PIN))
	
#define ZERO_DETECTOR_INIT_INT {set_bit(MCUCR,ISC00); unset_bit(MCUCR,ISC01); set_bit(GICR, INT0);}
#define ZERO_DETECTOR_INT_VECTOR INT0_vect


/*relay_0 description*/
#define RELAY_0_ID 1
#define RELAY_0_PORT C
#define RELAY_0_PIN  2

/*relay_0 controls*/
#define RELAY_0_INIT set_bit(DDRPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_ON set_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_OFF unset_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_TOGGLE flip_bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)
#define RELAY_0_STATE bit(OUTPORT(RELAY_0_PORT), RELAY_0_PIN)

#endif /* SOCKET_DIMMER */