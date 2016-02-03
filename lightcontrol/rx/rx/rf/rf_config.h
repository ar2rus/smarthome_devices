/*
 * rf_config.h
 *
 * Created: 27.01.2016 11:18:14
 *  Author: gargon
 */ 


#ifndef RF_CONFIG_H_
#define RF_CONFIG_H_

#define RF_PORT B
#define RF_PIN  3

/* timer for rx only*/
//#define RF_TIMER_PRESCALER 8
//#define RF_TIMER_INIT {TCCR0A = 0; TCCR0B = 0; set_bit(TCCR0B, CS01); TCNT0 = 0;}
	
#define RF_TIMER_PRESCALER 64
#define RF_TIMER_INIT {TCNT0 = 0; TCCR0A = 0; TCCR0B = 0; set_bit2(TCCR0B, CS01, CS00);}

#define RF_TIMER_REG TCNT0


#endif /* RF_CONFIG_H_ */