.include "common\common.inc"
.include "common\DHT22.inc"
.include "common\NRF24L01.inc"

.cseg
.org 0x00
	rjmp reset
.org ADCCaddr
	rjmp adcComplete
.org WATCHDOGaddr
	rjmp wd
.org INT_VECTORS_SIZE
	rjmp reset

.include "common\common.asm"
.include "common\DHT22.asm"
.include "common\NRF24L01.asm"


wd:
	cli
reti

adcComplete:
	cli

	brts adcVoltage
		lds r17, lightness_data + 0
		in r16, ADCL
		add r17, r16
		sts lightness_data + 0, r17

		sts tmp_lightness_data + 0, r16

		lds r17, lightness_data + 1
		in r16, ADCH
		adc r17, r16
		sts lightness_data + 1, r17

		sts tmp_lightness_data + 1, r16
reti
	adcVoltage:
		in r16, ADCL
		sts voltage_data + 0, r16

		in r16, ADCH
		sts voltage_data + 1, r16
reti

reset:
	ldi r16, low(RAMEND)
	out SPL, r16
	;ldi r16, high(RAMEND)
	;out SPH, r16

	ldi r16, (1<<PB2)	;подтягиваем висящий порт
	out PORTB, r16
	
	ldi r16, (1<<PB0)	;питание DHT22
	out DDRB, r16

	ldi r16, (1<<PA7)	;питание фоторезистора
	out DDRA, r16

	rcall NRF24L01_Init ;инициализация NRF24L01

	clr r16
	sts lightness_data_valid, r16	;данные первого цикла по освещению не валидны (нужно накопить 8 измерений, одно переносится с предыдущего такта)

	ldi r23, 7		;номер цикла (8-секундый цикл WD), не использовать больше нигде R23	(7*8=56 сек + 2сек инициал DHT22 + измерения + отправка ~= 1 minute)
	ldi r24, 30		;номер цикла (минутный цикл отправки данных), не использовать больше нигде R24. Для измерения напряжения питания раз в 30 мин.

while:
	;измеряем освещенность
	sbi PORTA, PA7	 ;подаем питание на фоторезистор

	ldi r16,(0<<REFS1)|(0<<REFS0)|(1<<MUX2)|(1<<MUX1)			;вход АЦП PA6, ref=VCC
	out ADMUX,r16
	
	//ldi r16,(1<<ADLAR)
	//out ADCSRB,r16
	
	;ADLAR = 0
	clr r16
	out ADCSRB, r16

	;enable ADC; auto trigger disable; interrupt; division factor = 8; start conversion
	ldi r16, (1<<ADEN)|(1<<ADSC)|(0<<ADATE)|(1<<ADIE)|(1<<ADPS1)|(1<<ADPS0)
	out ADCSRA, r16

	in r16, MCUCR
	;ADC noise reduction mode
	ori r16, (1<<SE)|(1<<SM0)
	andi r16, ~(1<<SM1)
	out MCUCR, r16
	
	clt		;используем флаг t для определения типа преобразования в прерывании (освещенность)

	sei
	sleep

	clr r16
	out ADCSRA, r16  ;disable ADC

	cbi PORTA, PA7   ;убираем питание с фоторезистора



	cpi r23, 7		;проверка номера цикла
	brne br_sleep

	;раз в минуту измеряем температуру/влажность
	sbi PORTB, PB0 ;подаем питание на DHT22

	;засыпаем на 2 секунды, пока инициализируется DHT (не ставить меньше 2-ух секунд)
	in r16, MCUCR
	;shutdown mode
	ori r16, (1<<SE)|(1<<SM1)
	andi r16, ~(1<<SM0)
	out MCUCR, r16
	
	ldi r16, (1<<WDIE) | (0<<WDP3) | (1<<WDP2) | (1<<WDP1) | (1<<WDP0)	;2sec
	out WDTCSR, r16

	sei
	sleep
	
    rcall DHTRead

	cbi PORTB, PB0 ;вырубаем питание на DHT22
	cbi PORTB, PB1 ;может лучше на ввод и подтянуть?

	cpi r24, 30
	brne br_transmit
	clr r24

	;измеряем напряжение питания
	ldi r16, (1<<ADEN)|(0<<ADATE)|(1<<ADIE)|(1<<ADPS1)|(1<<ADPS0)
	out ADCSRA, r16

	ldi r16, (0<<REFS1)|(0<<REFS0)|(1<<MUX5)|(1<<MUX0)		;1.1V, ref=VCC
	out ADMUX, r16
	rcall delay_2ms	;2ms на прогрев

	;ADLAR = 0
	//clr r16
	//out ADCSRB, r16

	;start ADC
	in r16, ADCSRA
	ori r16, (1<<ADSC)
	out ADCSRA, r16

	in r16, MCUCR
	ori r16, (1<<SE)|(1<<SM0)
	andi r16, ~(1<<SM1)
	out MCUCR, r16
	
	set		;используем флаг t для определения типа преобразования в прерывании (напряжение питания)

	sei
	sleep

	clr r16
	out ADCSRA, r16  ;disable ADC

	ldi r16, 1
	sts voltage_data_valid, r16		;данные по напряжению валидны
	//перевод данных в проценты
	rcall calcVoltageValue
	

	br_transmit:
	;отправляем данные
	rcall NRF24L01_StartWrite

	//перевод данных по освещению в проценты
	rcall calcLightnessPercent

	//проверка валидности данных DHT по напряжению (не валидны, если меньше 3 Вольт)
	rcall checkValidityDHT22ByVoltage

	//копируем данные
	clr   r27						;clear high X byte
	ldi   r26, nrf24_spi_buffer_tx	;set low X byte to nrf24_spi_buffer_tx pointer

	clr   r29						;clear high Y byte
	ldi   r28, DHT22_data_valid		;set low Y byte to DHT22_valid pointer

	ldi r17, 12
	copyData:
	ld r16, Y+
	st X+, r16
	dec r17
	brne copyData

	rcall NRF24L01_Write
	rcall NRF24L01_PowerDown

	//reset counter and temporary data
	inc r24
	rcall resetCycleData

	;отключаемся на 8 секунд
	br_sleep:
		in r16, MCUCR
		;shutdown mode
		ori r16, (1<<SE)|(1<<SM1)
		andi r16, ~(1<<SM0)
		out MCUCR, r16
	
		ldi r16, (1<<WDIE) | (1<<WDP3) | (0<<WDP2) | (0<<WDP1) | (1<<WDP0)			;8sec	
		//ldi r16, (1<<WDIE) | (0<<WDP3) | (1<<WDP2) | (1<<WDP1) | (0<<WDP0)		;1sec	
		//ldi r16, (1<<WDIE) | (0<<WDP3) | (0<<WDP2) | (0<<WDP1) | (0<<WDP0)		;быстро
		out WDTCSR, r16

		sei
		sleep

		inc r23
	rjmp while

	;вынесено только с целью избежания ошибки out of branch
	resetCycleData:
		clr r23
		//sts lightness_data + 0, r23
		//sts lightness_data + 1, r23
		//переносим последнее измерение в текущюю сумму
		lds r16, tmp_lightness_data + 0
		sts lightness_data + 0, r16
		lds r16, tmp_lightness_data + 1
		sts lightness_data + 1, r16
	
		sts voltage_data_valid, r23		;теперь данные по напряжению не валидны
		ldi r16, 1
		sts lightness_data_valid, r16	;теперь данные по освещению валидны
	ret

	;расчет процента освещенности 
	;по сумме результатов 8 измерений
	;ADC value = 0    - 0%
	;ADC value = 1023 - 100%
	calcLightnessPercent:
		lds r16, lightness_data + 0
		lds r17, lightness_data + 1

		ldi r18, ((10000>>0) & 0xFF)
		ldi r19, ((10000>>8) & 0xFF)

		rcall mpy16u

		mov r16, r18
		mov r17, r19
		mov r18, r20
		mov r19, r21

		ldi r20, ((8184>>0) & 0xFF); 8*1023
		ldi r21, ((8184>>8) & 0xFF)

		rcall DivLongToWord

		sts lightness_data + 0, r16
		sts lightness_data + 1, r17
	ret


	;расчет напряжения питания обратным вычислением
	;опорное напряжение 1.1 V (его измеряли относительно напряжения питания)
	; 1.1V - ADC value
	; x V - 1023
	calcVoltageValue:
		ldi r16, ((1125300>> 0) & 0xFF)
		ldi r17, ((1125300>> 8) & 0xFF)
		ldi r18, ((1125300>>16) & 0xFF)
		ldi r19, ((1125300>>24) & 0xFF)

		lds r20, voltage_data + 0
		lds r21, voltage_data + 1

		rcall DivLongToWord

		sts voltage_data + 0, r16
		sts voltage_data + 1, r17
	ret

	;проверка валидности данных DHT22 по напряжению питания,
	;не валидны, если меньше 3 Вольт
	checkValidityDHT22ByVoltage:
		lds r16, voltage_data + 0
		ldi r17, (3000>>8) & 0xFF
		cp r16, r17

		brlo dht_invalid
		brne dht_valid
		lds r16, voltage_data + 1
		ldi r17, (3000>>0) & 0xFF
		cp r16, r17
		
		brsh dht_valid
		dht_invalid: 
		clr r16
		sts DHT22_data_valid, r16
		dht_valid:
	ret