/*
 * fan_config.h
 *
 * Created: 21.09.2015 17:12:47
 *  Author: gargon
 */ 


#ifndef FAN_CONFIG_H_

#include <avr/io.h>

//периодчность опроса датчика влажности (в секундах)
#define FAN_HUMIDITY_CHECK_TIME 30

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

//значение влажности, при котором не производится 
//автоматическое включения вентилятора
#define HUMIDITY_MIN_ABS 50

//адрес в EEPROM для хранения конфиг. параметров:
//тек.режим
#define EEPROM_ADDRESS_FAN_CONFIG_MODE 0x00

#define FAN_CONFIG_H_


#endif /* FAN_CONFIG_H_ */