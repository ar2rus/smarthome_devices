/*
 * Wardrobe.h
 *
 * Created: 10.06.2015
 *  Author: gargon
 */ 


#ifndef WARDROBE_H_
#define WARDROBE_H_

#include "utils/bits.h"
#include "clunet/clunet.h"


/*2 свободных порта используются как дополнительные землся и питания
только для удобства подключения датчиков (не в одну клемму)*/
#define DOOR_ADD_VCC_PORT B
#define DOOR_ADD_VCC_PIN 3

#define DOOR_ADD_GND_PORT B
#define DOOR_ADD_GND_PIN 4

#define DOOR_ADD_VCC_INIT {set_bit(DDRPORT(DOOR_ADD_VCC_PORT), DOOR_ADD_VCC_PIN); set_bit(OUTPORT(DOOR_ADD_VCC_PORT), DOOR_ADD_VCC_PIN);}
#define DOOR_ADD_GND_INIT {set_bit(DDRPORT(DOOR_ADD_GND_PORT), DOOR_ADD_GND_PIN); unset_bit(OUTPORT(DOOR_ADD_GND_PORT), DOOR_ADD_GND_PIN);}
	
#define DOOR_ADD_INIT {DOOR_ADD_VCC_INIT; DOOR_ADD_GND_INIT;}



#define DOOR_LEFT_SENSOR_PORT B
#define DOOR_LEFT_SENSOR_PIN 3

#define DOOR_LEFT_SENSOR_INIT unset_bit(DDRPORT(DOOR_LEFT_SENSOR_PORT), DOOR_LEFT_SENSOR_PIN)
#define DOOR_LEFT_SENSOR_READ (bit(INPORT(DOOR_LEFT_SENSOR_PORT), DOOR_LEFT_SENSOR_PIN))


#define DOOR_RIGHT_SENSOR_PORT C
#define DOOR_RIGHT_SENSOR_PIN 0

#define DOOR_RIGHT_SENSOR_INIT unset_bit(DDRPORT(DOOR_RIGHT_SENSOR_PORT), DOOR_RIGHT_SENSOR_PIN)
#define DOOR_RIGHT_SENSOR_READ (bit(INPORT(DOOR_RIGHT_SENSOR_PORT), DOOR_RIGHT_SENSOR_PIN))


#define DOORS_SENSORS_INIT {DOOR_LEFT_SENSOR_INIT; DOOR_RIGHT_SENSOR_INIT;}
#define DOORS_SENSORS_READ ((DOOR_LEFT_SENSOR_READ<<1) | (DOOR_RIGHT_SENSOR_READ<<0))

/* main timer controls*/
#define TIMER_PRESCALER 64
#define TIMER_NUM_TICKS (unsigned int)(1e-3 * F_CPU / TIMER_PRESCALER)	/*1ms main loop*/
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit2(TCCR1B, CS11, CS10); unset_bit(TCCR1B, CS12); /*64x prescaler*/}

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

//#define ENABLE_TIMER_CMP_B set_bit(TIMSK, OCIE1B)
//#define DISABLE_TIMER_CMP_B unset_bit(TIMSK, OCIE1B)

#endif /* WARDROBE_H_ */