/*
 *  Audio.h
 *
 *  Created: 30.10.2014 21:04:37
 *  Author: gargon
 */ 


#ifndef AUDIO_H_
#define AUDIO_H_

#include "utils/bits.h"
#include "clunet/clunet.h"
#include "clunet/clunet_buffered.h"
#include "lc75341/lc75341.h"
#include "nec/rx.h"
#include "tea5767/TEA5767.h"

#include <avr/io.h>
#include <avr/interrupt.h>

/*led port description*/
#define LED_PORT C
#define LED_PIN  1

/*encoder ports description*/
#define ENCODER_1_PORT B
#define ENCODER_1_PIN  4

#define ENCODER_2_PORT B
#define ENCODER_2_PIN  5

/*buttons ports description*/
#define BUTTON1_PORT B
#define BUTTON1_PIN  2

#define BUTTON2_PORT B
#define BUTTON2_PIN  1

#define BUTTON3_PORT B
#define BUTTON3_PIN  0

#define BUTTON4_PORT D
#define BUTTON4_PIN  7

#define BUTTON_L_PORT D
#define BUTTON_L_PIN  6

#define BUTTON_R_PORT D
#define BUTTON_R_PIN  5

/*led controls*/
#define LED_INIT set_bit(DDRPORT(LED_PORT), LED_PIN)
#define LED_ON   set_bit(OUTPORT(LED_PORT), LED_PIN)
#define LED_OFF  unset_bit(OUTPORT(LED_PORT), LED_PIN)

/*button controls*/
#define BUTTON_INIT(port, pin) {unset_bit(DDRPORT(port), pin); set_bit(OUTPORT(port), pin);}
#define BUTTON1_INIT  BUTTON_INIT(BUTTON1_PORT, BUTTON1_PIN)
#define BUTTON2_INIT  BUTTON_INIT(BUTTON2_PORT, BUTTON2_PIN)
#define BUTTON3_INIT  BUTTON_INIT(BUTTON3_PORT, BUTTON3_PIN)
#define BUTTON4_INIT  BUTTON_INIT(BUTTON4_PORT, BUTTON4_PIN)
#define BUTTON_L_INIT BUTTON_INIT(BUTTON_L_PORT, BUTTON_L_PIN)
#define BUTTON_R_INIT BUTTON_INIT(BUTTON_R_PORT, BUTTON_R_PIN)
#define BUTTONS_INIT {BUTTON1_INIT; BUTTON2_INIT; BUTTON3_INIT; BUTTON4_INIT; BUTTON_L_INIT; BUTTON_R_INIT;}

#define BUTTON_READ(port, pin) (!(INPORT(port) & _BV(pin)))
#define BUTTON1_READ  (BUTTON_READ(BUTTON1_PORT, BUTTON1_PIN))
#define BUTTON2_READ  (BUTTON_READ(BUTTON2_PORT, BUTTON2_PIN))
#define BUTTON3_READ  (BUTTON_READ(BUTTON3_PORT, BUTTON3_PIN))
#define BUTTON4_READ  (BUTTON_READ(BUTTON4_PORT, BUTTON4_PIN))
#define BUTTON_L_READ (BUTTON_READ(BUTTON_L_PORT, BUTTON_L_PIN))
#define BUTTON_R_READ (BUTTON_READ(BUTTON_R_PORT, BUTTON_R_PIN))


/* main timer controls*/
#define TIMER_PRESCALER 64
#define TIMER_NUM_TICKS (unsigned int)(1e-3 * F_CPU / TIMER_PRESCALER)	/*1ms main loop*/
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit2(TCCR1B, CS11, CS10); unset_bit(TCCR1B, CS12); /*64x prescaler*/}

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define ENABLE_TIMER_CMP_B set_bit(TIMSK, OCIE1B)
#define DISABLE_TIMER_CMP_B unset_bit(TIMSK, OCIE1B)

/* skip events delay */
#define TIMER_SKIP_EVENTS_DELAY (unsigned int)(150e-3 * F_CPU / TIMER_PRESCALER)	/*150ms - не уменьшать, начинает глючит clunet*/

/* i2c controls */
#define TWI_INIT {set_bit(OUTPORT(C), 4); set_bit(OUTPORT(C), 5);}


#endif /* AUDIO_H_ */