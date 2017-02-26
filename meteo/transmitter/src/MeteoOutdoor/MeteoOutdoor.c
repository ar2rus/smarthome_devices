/*
 * MeteoOutdoor.c
 *
 * Created: 09.01.2017 13:26:45
 *  Author: gargon
 */ 


#include <avr/io.h>
#include <util/delay.h>
#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

#include "VirtualWire/VirtualWire.h"
#include "USI_TWI/USI_TWI_Master.h"
#include "bme280/bme280.h"
#include "bht1750/bht1750.h"

char data[14];

ISR (WDT_vect) {
	//wdt_reset();
	//WDTCSR |= _BV(WDIE); // разрешаем прерывания по ватчдогу. Иначе будет резет.
}

int main(void){
	sei();
	set_sleep_mode(SLEEP_MODE_PWR_DOWN); // если спать - то на полную
	
	USI_TWI_Master_Initialise();
	vw_setup(2000);
	
	//REFS1..0 = 1 AREF вход как источник опорного напряжения
	//MUX5..0 = 100001 измеряем внутренние 1.1V
	ADMUX = (1<<REFS0)|(1<<MUX5)|(1<<MUX0);
	//wait 1 ms at least
	
	
	while(1){
		 bme280_start_force(1, 1, 1);
		 bht1750_start(BH1750_ONE_TIME_HIGH_RES_MODE);
		 
		 //по факту 2,42 - 2,38  сек
		 wdt_enable(WDTO_2S);
		 WDTCSR |= _BV(WDIE);  // разрешаем прерывания по ватчдогу. Иначе будет резет.
		 
		 sleep_enable(); // разрешаем сон
		 sleep_cpu();    // спать!
		 
		 
		 //АЦП для измерения напряжения батареи
		 
		 //ADEN = 1 - разрешаем АЦП
		 //ADSC = 1 Запускаем преобразование
		 //ADPS2..0 = 6 Делитель частоты на 128
		 ADCSRA = (1<<ADEN)|(1<<ADSC)|(7<<ADPS0);
		 
		 uint8_t size_0 = bme280_readValues((char*)(&data[0]));
		 if (size_0){
			 uint8_t size_1 = bht1750_readValues((char*)(&data[size_0]));
			 if (size_1){
				size_0 += size_1;
				
				//Ждем завершение преобразования АЦП
				while (!(ADCSRA & (1<<ADIF)));
				ADCSRA = 0;
				
				uint8_t lo = ADCL;
				uint8_t hi = ADCH;
				
				//обратное вычисление:
				//меряем внутренние 1,1В относительно AREF напряжения с батареи
				//По факту имеем примерно 1,16В
				//AREF -> 1023
				//1,16 -> x
				//AREF = 1.16 * 1023 / x
				
				//умножим на 100 и разделим на 2
				//потеряем разряд точности, но зато уместим в 2 байта
				//AREF = 59334 / (x/2)
				
				uint16_t t2 = ((hi<<8) | lo) >> 1;
				t2 = 59334 / t2;
				
				data[size_0++] = (t2 >> 8 ) & 0xFF;
				data[size_0++] = t2 & 0xFF;
				
				
				//отправляем 2 раза
				for (int i=0; i<2; i++){
					vw_send((uint8_t *)data, size_0);
					vw_wait_tx();
				}
			 }
		 }

		 //спим 56+- сек
		 for (int i=0; i<7; i++){
			 wdt_enable(WDTO_8S);
			 WDTCSR |= _BV(WDIE);  // разрешаем прерывания по ватчдогу. Иначе будет резет.
					  
			sleep_enable(); // разрешаем сон
			sleep_cpu();    // спать!
		 }
    }
}