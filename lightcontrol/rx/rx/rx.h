/*
 * rx.h
 *
 * Created: 26.01.2016 20:54:22
 *  Author: gargon
 */ 


#ifndef RX_H_
#define RX_H_

#include "utils/bits.h"
#include "rf/rf_const.h"
#include "nec/nec_const.h"

/*NEC*/

//pwm nec ports
#define NEC_PORT B
#define NEC_PIN  1

//#define PWMVAL_A (unsigned char)(F_CPU / 2000 / NEC_CARRIER)
#define PWMVAL_A 120
#define PWMVAL_B (unsigned char)(0.3 * PWMVAL_A)

#define NEC_HDR_MARK_T (unsigned int)(NEC_HDR_MARK * ((double)F_CPU / 1000000UL) / PWMVAL_A)
#define NEC_HDR_SPACE_T (unsigned int)(NEC_HDR_SPACE * ((double)F_CPU / 1000000UL) / PWMVAL_A)
#define NEC_BIT_MARK_T  (unsigned int)(NEC_BIT_MARK * ((double)F_CPU / 1000000UL) / PWMVAL_A)
#define NEC_ONE_SPACE_T (unsigned int)(NEC_ONE_SPACE * ((double)F_CPU / 1000000UL) / PWMVAL_A)
#define NEC_ZERO_SPACE_T (unsigned int)(NEC_ZERO_SPACE * ((double)F_CPU / 1000000UL) / PWMVAL_A)
#define NEC_RPT_SPACE_T (unsigned int)(NEC_RPT_SPACE * ((double)F_CPU / 1000000UL) / PWMVAL_A)

/* phase-correct PWM with OCRA as top, no prescaling*/
/* The top value for the timer.  The modulation frequency will be SYSCLOCK / 2 / OCR2A.*/
//and enable interrupts for timing
#define PWM_SETUP {TCCR0A = _BV(WGM00); TCCR0B = _BV(WGM02) | _BV(CS00); OCR0A = PWMVAL_A; OCR0B = PWMVAL_B; TIMSK0 = 0; set_bit2(TIMSK0, OCIE0A, TOIE0);}
#define NEC_INIT {set_bit(DDRPORT(NEC_PORT), NEC_PIN); unset_bit(OUTPORT(NEC_PORT), NEC_PIN); PWM_SETUP;}

#define TIMER_ENABLE_PWM  (TCCR0A |= _BV(COM0B1))
#define TIMER_DISABLE_PWM (TCCR0A &= ~(_BV(COM0B1)))


/*RF*/
#define RF_PORT B
#define RF_PIN  0

#define RF_NUM_TICKS_START  (unsigned int)((0.8 * RF_START_BIT1_DELAY) * ((double)F_CPU / 1000000UL) / PWMVAL_A)
#define RF_NUM_TICKS_BIT (unsigned int)((RF_BIT0_DELAY + RF_HI_DELAY + (RF_BIT1_DELAY - RF_BIT0_DELAY) / 2) * ((double)F_CPU / 1000000UL) / PWMVAL_A)
#define RF_RX_INIT {unset_bit(DDRPORT(RF_PORT), RF_PIN);}

#define RF_VAL (test_bit(INPORT(RF_PORT), RF_PIN))


//идентификатор лампы (0-9)
#define RGB_LIGHT_ID 5



// debugging
// #define LED_PORT B
// #define LED_PIN  4
// 
// #define LED_INIT {set_bit(DDRPORT(LED_PORT), LED_PIN); LED_OFF;}
// #define LED_ON set_bit(OUTPORT(LED_PORT), LED_PIN)
// #define LED_OFF unset_bit(OUTPORT(LED_PORT), LED_PIN)
 
 #endif /* RX_H_ */