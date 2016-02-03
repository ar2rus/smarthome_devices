/*
 * tx.h
 *
 * Created: 26.01.2016 20:40:22
 *  Author: gargon
 */ 


#ifndef TX_H_
#define TX_H_


#include "rf/rf.h"




//debugging
#define LED_PORT B
#define LED_PIN  4

#define BTN_INIT {unset_bit(DDRPORT(LED_PORT), LED_PIN); set_bit(OUTPORT(LED_PORT), LED_PIN);}
#define BTN_VAL (test_bit(INPORT(LED_PORT), LED_PIN))

#endif /* TX_H_ */