/*
 * tx.h
 *
 * Created: 26.01.2016 20:40:22
 *  Author: gargon
 */ 


#ifndef TX_H_
#define TX_H_


#include "utils/bits.h"

#define RF_PORT B
#define RF_PIN  3

#define RF_INIT {set_bit(DDRPORT(RF_PORT), RF_PIN); RF_LO;}
#define RF_HI set_bit(OUTPORT(RF_PORT), RF_PIN)
#define RF_LO unset_bit(OUTPORT(RF_PORT), RF_PIN)

#endif /* TX_H_ */