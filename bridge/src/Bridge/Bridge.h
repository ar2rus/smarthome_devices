/*
 * Bridge.h
 *
 * Created: 20.06.2020
 * Author: gargon
 */

#ifndef BRIDGE_H_
#define BRIDGE_H_

#include "utils/bits.h"
#include "clunet/clunet.h"
#include "clunet/clunet_buffered.h"

#include <avr/interrupt.h>
#include <avr/wdt.h>

typedef enum{
	OFF,
	NUMBERS,
	DASHES
} DISPLAY_MODE;

typedef struct {
	DISPLAY_MODE mode;
	char number;
	char sign;
	char led_on;
} display_t;

#define SPI_PORT B
#define SPI_DAT 3	//MOSI (SI)
#define SPI_CLK 5	//SCK (SCK)
#define SPI_LTC 2	//SS (RCK)

#define SPI_SET_OUTPUT set_bit3(DDRPORT(SPI_PORT), SPI_DAT, SPI_CLK, SPI_LTC)
#define SPI_SET_LOW unset_bit3(OUTPORT(SPI_PORT), SPI_DAT, SPI_CLK, SPI_LTC)
#define SPI_MASTER SPCR = (1 << SPE) | (1 << MSTR)

#define SPI_INIT {SPI_SET_OUTPUT; SPI_SET_LOW; SPI_MASTER; }
#define SPI_SEND(code) {SPDR = ~code; while(!test_bit(SPSR, SPIF)); set_bit(OUTPORT(SPI_PORT), SPI_LTC); unset_bit(OUTPORT(SPI_PORT), SPI_LTC);}

#define SIGN0_PORT B
#define SIGN0_PIN 1
#define SIGN1_PORT B
#define SIGN1_PIN 0

#define SIGNS_OFF {unset_bit(OUTPORT(SIGN0_PORT), SIGN0_PIN); unset_bit(OUTPORT(SIGN1_PORT), SIGN1_PIN);}
#define SIGNS_INIT {set_bit(DDRPORT(SIGN0_PORT), SIGN0_PIN); set_bit(DDRPORT(SIGN1_PORT), SIGN1_PIN); SIGNS_OFF;}
#define SIGN_ENABLE(port, pin, on) {SIGNS_OFF; if (on) set_bit(OUTPORT(port), pin);}
#define SIGN0_ENABLE SIGN_ENABLE(SIGN0_PORT, SIGN0_PIN, 1)
#define SIGN1_ENABLE SIGN_ENABLE(SIGN1_PORT, SIGN1_PIN, 1)

#define SIGN0(code) {SIGN0_ENABLE; SPI_SEND(code);}
#define SIGN1(code) {SIGN1_ENABLE; SPI_SEND(code);}
	
#define DISPLAY_INIT {SIGNS_INIT; SPI_INIT;}


#define SYMBOL_D0 0b11111010
#define SYMBOL_D1 0b00011000
#define SYMBOL_D2 0b10101110
#define SYMBOL_D3 0b00111110
#define SYMBOL_D4 0b01011100
#define SYMBOL_D5 0b01110110
#define SYMBOL_D6 0b11110110
#define SYMBOL_D7 0b00011010
#define SYMBOL_D8 0b11111110
#define SYMBOL_D9 0b01111110
#define SYMBOL_DASH 0b100

char const SYMBOL_DIGIT[] = {SYMBOL_D0, SYMBOL_D1, SYMBOL_D2, SYMBOL_D3, SYMBOL_D4, SYMBOL_D5, SYMBOL_D6, SYMBOL_D7, SYMBOL_D8, SYMBOL_D9};
	
#define CODE(symbol, led_on) (symbol | !led_on)
	



#define BUTTON_PORT D
#define BUTTON_PIN 5

#define BUTTON_INIT {unset_bit(DDRPORT(BUTTON_PORT), BUTTON_PIN); set_bit(OUTPORT(BUTTON_PORT), BUTTON_PIN);}
#define BUTTON_READ (bit(INPORT(BUTTON_PORT), BUTTON_PIN))

/* main timer controls*/
#define TIMER_PRESCALER 64
#define TIMER_PERIOD 1	//1ms
#define TIMER_NUM_TICKS (unsigned int)(TIMER_PERIOD *1e-3 * F_CPU / TIMER_PRESCALER)
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit2(TCCR1B, CS11, CS10); unset_bit(TCCR1B, CS12);} //64x prescaler

#define TIMER_REG TCNT1

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define TIMER_COMP_VECTOR TIMER1_COMPA_vect

#endif /* BRIDGE_H_ */