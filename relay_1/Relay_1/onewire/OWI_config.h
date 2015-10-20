/*
 * OWI_config.h
 *
 * Created: 12.05.2015 11:49:12
 *  Author: gargon
 */ 


#ifndef OWI_CONFIG_H_
#define OWI_CONFIG_H_

#include "onewire/OWIPolled.h"
#include <avr/io.h>


//количество устройств на шине 1Wire
#define OWI_MAX_BUS_DEVICES       3

// Port configuration registers for 1-Wire buses.
// Make sure that all three registers belong to the same port.
#define     OWI_PORT        PORTC		//!< 1-Wire PORT Data register.
#define     OWI_PIN         PINC		//!< 1-Wire Input pin register.
#define     OWI_DDR         DDRC		//!< 1-Wire Data direction register.
#define		OWI_BUS			OWI_PIN_0


#endif /* OWI_CONFIG_H_ */