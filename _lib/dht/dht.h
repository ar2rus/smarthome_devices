/*
DHT Library 0x03

copyright (c) Davide Gironi, 2012

Released under GPLv3.
Please refer to LICENSE file for licensing information.

References:
  - DHT-11 Library, by Charalampos Andrianakis on 18/12/11
*/


#ifndef DHT_H_
#define DHT_H_

#include "dht_config.h"
#include "utils/bits.h"

#include <avr/io.h>

//sensor type
#define DHT_DHT11 1
#define DHT_DHT22 2

#define DHT_OUTPUT set_bit(DDRPORT(DHT_PORT), DHT_PIN)
#define DHT_INPUT unset_bit(DDRPORT(DHT_PORT), DHT_PIN)
#define DHT_HIGH set_bit(OUTPORT(DHT_PORT), DHT_PIN)
#define DHT_LOW unset_bit(OUTPORT(DHT_PORT), DHT_PIN)
#define DHT_TEST (test_bit(INPORT(DHT_PORT), DHT_PIN))

//timeout retries
#define DHT_TIMEOUT 200

extern int8_t dht_gettemperature(int16_t *temperature);
extern int8_t dht_gethumidity(int16_t *humidity);
extern int8_t dht_gettemperaturehumidity(int16_t *temperature, int16_t *humidity);
	
/**************************************************************************/
/* Дополнительные функции, использующие закэшированные значения с датчиков*/
/* дополнительно передается системное время в мс						  */
/**************************************************************************/
extern int8_t dht_gettemperature_cached(int16_t *temperature, uint32_t systime);
extern int8_t dht_gethumidity_cached(int16_t *humidity, uint32_t systime);
extern int8_t dht_gettemperaturehumidity_cached(int16_t *temperature, int16_t *humidity, uint32_t systime);


//cache timeout (ms)
#define DHT_CACHE_TIMEOUT 2000

#endif /* DHT_H_ */