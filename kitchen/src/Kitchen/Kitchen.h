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

#define DHT_SENSOR_ID 2

/* main timer controls*/
#define TIMER_PRESCALER 8
#define TIMER_DELAY 1e-3	/*1ms main loop*/
#define TIMER_NUM_TICKS (unsigned int)(TIMER_DELAY * F_CPU / TIMER_PRESCALER)
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit(TCCR1B, CS11); unset_bit2(TCCR1B, CS10, CS12); /*8x prescaler*/}

#define TIMER_REG TCNT1

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define ENABLE_TIMER_CMP_B set_bit(TIMSK, OCIE1B)
#define DISABLE_TIMER_CMP_B unset_bit(TIMSK, OCIE1B)

#define TIMER_COMP_VECTOR TIMER1_COMPA_vect
#define TIMER_COMP_B_VECTOR TIMER1_COMPB_vect



#define BUTTON_ID 1
#define BUTTON_LONG_ID 2

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

#define CLUNET_KITCHEN_LIGHT_ID 0x82

//максимальная длина номера, набираемого с пульта
#define NUMBER_DIAL_MAX_LENGTH 20
//таймаут набора номера с пульта (после нажатия последнего символа)
#define NUMBER_DIAL_TIMEOUT 2000

#endif /* BATH_SENSORS_H_ */