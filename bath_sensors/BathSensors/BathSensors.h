/*
 * Relay_1.h
 *
 * Created: 10.06.2015
 *  Author: gargon
 */ 


#ifndef BATH_SENSORS_H_
#define BATH_SENSORS_H_

#include "utils/bits.h"
#include "nec/rx.h"
#include "dht/dht.h"
#include "clunet/clunet.h"
#include "clunet/clunet_buffered.h"

#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>


#define DOOR_SENSOR_PORT D
#define DOOR_SENSOR_PIN 5

#define DOOR_SENSOR_INIT unset_bit(DDRPORT(DOOR_SENSOR_PORT), DOOR_SENSOR_PIN)
#define DOOR_SENSOR_READ (!bit(INPORT(DOOR_SENSOR_PORT), DOOR_SENSOR_PIN))

#define DHT_SENSOR_ID 1

/*ADC0*/
#define ADC_CHANNEL ADC0
#define ADC_INIT {set_bit2(ADMUX, REFS0, ADLAR); /*ref = vcc, 8 bit precision, ADC0*/	\
				  set_bit3(ADCSRA, ADEN, ADIE, ADPS2); /*enable, interruptions, div 16*/}

/* main timer controls*/
#define TIMER_PRESCALER 64
#define TIMER_NUM_TICKS (unsigned int)(1e-3 * F_CPU / TIMER_PRESCALER)	/*1ms main loop*/
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit2(TCCR1B, CS11, CS10); unset_bit(TCCR1B, CS12); /*64x prescaler*/}

#define TIMER_REG TCNT1

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define ENABLE_TIMER_CMP_B set_bit(TIMSK, OCIE1B)
#define DISABLE_TIMER_CMP_B unset_bit(TIMSK, OCIE1B)

#define TIMER_COMP_A_VECTOR TIMER1_COMPA_vect
#define TIMER_COMP_B_VECTOR TIMER1_COMPB_vect

/* skip events delay */
#define TIMER_SKIP_EVENTS_DELAY (unsigned int)(150e-3 * F_CPU / TIMER_PRESCALER)	/*150ms - не уменьшать, начинает глючит clunet*/


#define LIGHTNESS_BARRIER 10	/*percent*/

#define AUDIOBATH_DEVICE_ID 0x0B
#define FAN_DEVICE_ID 0x15

#endif /* BATH_SENSORS_H_ */