/*
 * rx.h
 *
 * Created: 26.01.2016 20:54:22
 *  Author: gargon
 */ 


#ifndef RX_H_
#define RX_H_


#include "utils/bits.h"

#define RF_PORT B
#define RF_PIN  3

#define RF_INIT {unset_bit(DDRPORT(RF_PORT), RF_PIN);}
#define RF_VAL (test_bit(INPORT(RF_PORT), RF_PIN))

/* main timer controls*/
#define TIMER_PRESCALER 8
//#define TIMER_NUM_TICKS (unsigned int)(1e-3 * F_CPU / TIMER_PRESCALER)	/*1ms main loop*/
#define TIMER_INIT {TCNT0 = 0; TCCR0A = 0; TCCR0B = 0; set_bit(TCCR0B, CS01);}
#define TIMER_REG TCNT0

#define NUM_TICKS_200_MKS (unsigned int)(200e-6 * F_CPU / TIMER_PRESCALER)
#define NUM_TICKS_125_MKS (unsigned int)(125e-6 * F_CPU / TIMER_PRESCALER)


//debugging
#define LED_PORT B
#define LED_PIN  4

#define LED_INIT {set_bit(DDRPORT(LED_PORT), LED_PIN); LED_OFF;}
#define LED_ON set_bit(OUTPORT(LED_PORT), LED_PIN)
#define LED_OFF unset_bit(OUTPORT(LED_PORT), LED_PIN)

#endif /* RX_H_ */