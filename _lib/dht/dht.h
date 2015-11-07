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

//enable decimal precision (float)
#if DHT_TYPE == DHT_DHT11
	#define DHT_FLOAT 0
#elif DHT_TYPE == DHT_DHT22
	#define DHT_FLOAT 1
#endif

//timeout retries
#define DHT_TIMEOUT 200

//functions
#if DHT_FLOAT == 1
	extern int8_t dht_gettemperature(float *temperature);
	extern int8_t dht_gethumidity(float *humidity);
	extern int8_t dht_gettemperaturehumidity(float *temperature, float *humidity);
	
	/**************************************************************************/
	/* Дополнительные функции, использующие закэшированные значения с датчиков*/
	/* дополнительно передается системное время в мс						  */
	/**************************************************************************/
	extern int8_t dht_gettemperature_cached(float *temperature, uint32_t systime);
	extern int8_t dht_gethumidity_cached(float *humidity, uint32_t systime);
	extern int8_t dht_gettemperaturehumidity_cached(float *temperature, float *humidity, uint32_t systime);
#elif DHT_FLOAT == 0
	extern int8_t dht_gettemperature(int8_t *temperature);
	extern int8_t dht_gethumidity(int8_t *humidity);
	extern int8_t dht_gettemperaturehumidity(int8_t *temperature, int8_t *humidity);
	
	/**************************************************************************/
	/* Дополнительные функции, использующие закэшированные значения с датчиков*/
	/* дополнительно передается системное время в мс						  */
	/**************************************************************************/
	extern int8_t dht_gettemperature_cached(int8_t *temperature, uint32_t systime);
	extern int8_t dht_gethumidity_cached(int8_t *humidity, uint32_t systime);
	extern int8_t dht_gettemperaturehumidity_cached(int8_t *temperature, int8_t *humidity, uint32_t systime);
#endif

#endif

//cache timeout (ms)
#define DHT_CACHE_TIMEOUT 2000
