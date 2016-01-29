

#ifndef NEC_TX_H_
#define NEC_TX_H_

#include "nec_config.h"
#include "utils/bits.h"


#define NEC_TX_HI set_bit(OUTPORT(NEC_PORT), NEC_PIN)
#define NEC_TX_LO unset_bit(OUTPORT(NEC_PORT), NEC_PIN)
#define NEC_TX_INIT {set_bit(DDRPORT(NEC_PORT), NEC_PIN); NEC_TX_LO; }
	
void nec_send(char address, char command);
void nec_send_repeat();

#endif