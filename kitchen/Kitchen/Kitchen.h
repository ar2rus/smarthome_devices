/*
 * Relay_1.h
 *
 * Created: 10.06.2015
 *  Author: gargon
 */ 


#ifndef BATH_SENSORS_H_
#define BATH_SENSORS_H_

#include "utils/bits.h"
#include "dht/dht.h"
#include "clunet/clunet.h"

#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>


//#define MOTION_SENSOR_PORT D
//#define MOTION_SENSOR_PIN 5

//#define MOTION_SENSOR_INIT unset_bit(DDRPORT(MOTION_SENSOR_PORT), MOTION_SENSOR_PIN)
//#define MOTION_SENSOR_READ (bit(INPORT(MOTION_SENSOR_PORT), MOTION_SENSOR_PIN))

#define DHT_SENSOR_ID 2

/*ADC0*/
//#define ADC_CHANNEL ADC0
//#define ADC_INIT {ADMUX=(1<<REFS0)|(1<<ADLAR); /* ADC0; Aref=AVcc; 8 bit precision*/ ADCSRA=(1<<ADEN)|(1<<ADIE)|(1<<ADPS2); /*Prescalar div factor =16*/}

/* main timer controls*/
#define TIMER_PRESCALER 64
#define TIMER_NUM_TICKS (unsigned int)(1e-3 * F_CPU / TIMER_PRESCALER)	/*1ms main loop*/
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit2(TCCR1B, CS11, CS10); unset_bit(TCCR1B, CS12); /*64x prescaler*/}

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

//#define ENABLE_TIMER_CMP_B set_bit(TIMSK, OCIE1B)
//#define DISABLE_TIMER_CMP_B unset_bit(TIMSK, OCIE1B)

/* skip events delay */
//#define TIMER_SKIP_EVENTS_DELAY (unsigned int)(150e-3 * F_CPU / TIMER_PRESCALER)	/*150ms - не уменьшать, начинает глючит clunet*/


//#define LIGHTNESS_BARRIER 10	/*percent*/


#define BUTTON_ID 1

#define BUTTON_PORT D
#define BUTTON_PIN 5

#define BUTTON_INIT {unset_bit(DDRPORT(BUTTON_PORT), BUTTON_PIN); set_bit(OUTPORT(BUTTON_PORT), BUTTON_PIN);}
#define BUTTON_READ (!bit(INPORT(BUTTON_PORT), BUTTON_PIN))



#define EXHAUST_FAN_DEVICE_ID 1

#define HALL_SENSOR_PORT D
#define HALL_SENSOR_PIN 7

#define HALL_SENSOR_INIT {unset_bit(DDRPORT(HALL_SENSOR_PORT), HALL_SENSOR_PIN); set_bit(OUTPORT(HALL_SENSOR_PORT), HALL_SENSOR_PIN);}
#define HALL_SENSOR_READ (!bit(INPORT(HALL_SENSOR_PORT), HALL_SENSOR_PIN))


/*relay_0 description*/
#define RELAY_0_ID 1
#define RELAY_0_PORT C
#define RELAY_0_PIN  1

/*relay_1 description*/
#define RELAY_1_ID 2
#define RELAY_1_PORT B
#define RELAY_1_PIN  5


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

#define FAN_RELAY_ID RELAY_0_ID

#endif /* BATH_SENSORS_H_ */