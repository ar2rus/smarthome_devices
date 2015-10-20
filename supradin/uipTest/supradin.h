/*
 * supradin.h
 *
 * Created: 12.12.2014 18:21:30
 *  Author: gargon
 */ 
#ifndef SUPRADIN_H_

#define ETH_POLL_TIMER_INIT {TCCR0 = 0; TCNT0 = 0; unset_bit(TCCR0, CS01); set_bit2(TCCR0, CS02, CS00); /*1024x prescaler*/}

#define ETH_POLL_TIMER_PRESCALER 1024
#define ETH_ARP_TIMER_COUNTER 10 * F_CPU / 255 / ETH_POLL_TIMER_PRESCALER	/*10 sec*/
	
#define ENABLE_ETH_POLL_TIMER_OVF set_bit(TIMSK, TOIE0)
#define DISABLE_ETH_POLL_TIMER_OVF unset_bit(TIMSK, TOIE0)
	
#define SUPRADIN_H_

void ethernet_send();


#endif /* SUPRADIN_H_ */