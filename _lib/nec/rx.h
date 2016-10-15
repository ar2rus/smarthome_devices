/*
 * rx.h
 *
 * Created: 28.10.2014 22:02:48
 *  Author: gargon
 */ 


#ifndef NEC_RX_H_
#define NEC_RX_H_

#include "nec_config.h"
#include "utils/bits.h"

#include <avr/io.h>

#define NEC_TIMER_T 250	//check fronts every 50 mks

#define NEC_TIMER_CMP_TICKS (unsigned int) (F_CPU / NEC_TIMER_PRESCALER * NEC_TIMER_T * 1e-6)

#define NEC_NUM_TICKS_START_COND_1 (unsigned int) (13500 / NEC_TIMER_T - 8)		
#define NEC_NUM_TICKS_START_COND_2 (unsigned int) (13500 / NEC_TIMER_T + 8)

#define NEC_NUM_TICKS_REPEAT_COND_1 (unsigned int) (11250 / NEC_TIMER_T - 8)
#define NEC_NUM_TICKS_REPEAT_COND_2 (unsigned int) (11250 / NEC_TIMER_T + 8)

#define NEC_NUM_TICKS_LOG0_1 (unsigned int) (1120 / NEC_TIMER_T - 3)
#define NEC_NUM_TICKS_LOG0_2 (unsigned int) (1120 / NEC_TIMER_T + 2)

#define NEC_NUM_TICKS_LOG1_1 (unsigned int) (2250 / NEC_TIMER_T - 2)
#define NEC_NUM_TICKS_LOG1_2 (unsigned int) (2250 / NEC_TIMER_T + 3)

#define NEC_NUM_TICKS_TIMEOUT (unsigned int) (125000 / NEC_TIMER_T)

#define NEC_NUM_TICKS_WAIT_REPEAT (unsigned int) (50000 / NEC_TIMER_T)

//do not change the order of bits
#define NEC_STATUS_BIT_CNT	   0	//5 bits (3-4)
#define NEC_STATUS_START_COND  5
#define NEC_STATUS_START_SCAN  6
#define NEC_STATUS_PIN		   7


uint8_t necCheckSignal();
uint8_t necReadSignal(); 
uint8_t necValue(uint8_t *address, uint8_t *command);
void necResetValue();
 
#endif /* NEC_RX_H_ */