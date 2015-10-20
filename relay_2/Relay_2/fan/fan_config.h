/*
 * fan_config.h
 *
 * Created: 21.09.2015 17:12:47
 *  Author: gargon
 */ 


#ifndef FAN_CONFIG_H_

#include <avr/io.h>

//периодчность опроса датчика влажности (в секундах)
#define FAN_HUMIDITY_CHECK_TIME 15

#if FAN_HUMIDITY_CHECK_TIME < 5
#  error Humidity check time too frequent, decrease it
#endif

#if FAN_HUMIDITY_CHECK_TIME > 60
#  error Humidity check time too rare, increase it
#endif

//максимальное время работы вентилятора (в секундах), 30 мин
#define FAN_TIMEOUT 1800

//размер хранимой истории измерений (кол-во измерений в минуту)
#define HUMIDITY_HISTORY_COUNT 60 / FAN_HUMIDITY_CHECK_TIME

//абсолютный прирост текущего уровня влажности относительно среднего, вычисленного по истории,
//после которого требуется включение вентилятора
#define HUMIDITY_DELTA_PLUS 3
//значение влажности, при котором требуется включение вентилятора
//без отслеживания динамики ее роста
#define HUMIDITY_MAX_ABS 85


/* fan main timer controls*/
#define FAN_TIMER_PRESCALER 256
#define FAN_TIMER_NUM_TICKS (unsigned int)(1 * F_CPU / FAN_TIMER_PRESCALER)	/*1 second main loop*/
#define FAN_TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = FAN_TIMER_NUM_TICKS; set_bit(TCCR1B, CS12); unset_bit2(TCCR1B, CS11, CS10); /*256x prescaler*/}

#define FAN_TIMER_REG TCNT1

#define ENABLE_FAN_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_FAN_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define FAN_TIMER_COMP_VECTOR TIMER1_COMPA_vect

#define FAN_CONFIG_H_





#endif /* FAN_CONFIG_H_ */