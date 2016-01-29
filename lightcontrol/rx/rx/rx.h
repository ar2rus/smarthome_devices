/*
 * rx.h
 *
 * Created: 26.01.2016 20:54:22
 *  Author: gargon
 */ 


#ifndef RX_H_
#define RX_H_

#include "utils/bits.h"
#include "rf/rf.h"
#include "nec/tx.h"


//debugging
#define LED_PORT B
#define LED_PIN  4

#define LED_INIT {set_bit(DDRPORT(LED_PORT), LED_PIN); LED_OFF;}
#define LED_ON set_bit(OUTPORT(LED_PORT), LED_PIN)
#define LED_OFF unset_bit(OUTPORT(LED_PORT), LED_PIN)

#endif /* RX_H_ */