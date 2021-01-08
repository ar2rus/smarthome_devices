; delay_counter*3/1000000 = 0,000003*delay_counter s
delay_3mks:
	push r16

	lds r16, delay_counter
	
	delay_n3:
		subi r16, 1
		brne delay_n3

	pop r16
ret

; 256*(delay_counter+1)*4/1000000 = 0,001024*(delay_counter+1) s
delay_1024mks:
	push r16
	push r17

	ldi r16, 255
	lds r17, delay_counter
	
	delay_n2:
		subi r16, 1
		sbci r17, 0
		brcc delay_n2
	
	pop r17
	pop r16
ret

delay_2ms:
	rcall delay_1024mks
	rcall delay_1024mks
ret

; 256*256*(delay_counter+1)*5/1000000 = 0.328*(delay_counter+1) s
/*delay_328ms:
	push r16
	push r17
	push r18

	ldi r16, 255
	ldi r17, 255
	lds r18, delay_counter
	
	delay_n1:
		subi r16, 1
		sbci r17, 0
		sbci r18, 0
		brcc delay_n1
	
	pop r18
	pop r17
	pop r16
ret*/

/*delay_second:
	push r16
	ldi r16, 2
	sts delay_counter, r16
	rcall delay_328ms
	pop r16
ret*/





;***************************************************************************
; Беззнаковое умножение 16b x 16b
; [r16:r17]x[r18:r19] = [r18:r21]
;***************************************************************************

mpy16u:	
	clr	r21		;clear 2 highest bytes of result
	clr	r20
	ldi	r22,16	;init loop counter
	lsr	r19
	ror	r18

m16u_1:	
	brcc noad8		;if bit 0 of multiplier set
	add	r20,r16	;add multiplicand Low to byte 2 of res
	adc	r21,r17	;add multiplicand high to byte 3 of res
noad8:	
	ror	r21		;shift right result byte 3
	ror	r20		;rotate right result byte 2
	ror	r19		;rotate result byte 1 and multiplier High
	ror	r18		;rotate result byte 0 and multiplier Low
	dec	r22		;decrement loop counter
	brne m16u_1		;if not done, loop more
	ret


; =============================================================================
;
; Беззнаковое деление четырёхбайтовой величины на двухбайтовую
;
; =============================================================================
;
; Параметры:
;
;   R19:R16 - делимое
;   R21:R20 - делитель
;
; Возвращаемый результат:
;
;   C=1 - ошибка
;   C=0 - деление выполнено
;   R17:R16 - частное
;   R19:R18 - остаток
;
; Изменяемые регистры: R16-R19, R22

DivLongToWord:
        ldi     R22, 17
        cp      R21, R19
        brcs    _DWRet
        brne    _DW10
        cp      R18, R20
        brcc    _DWCS
        clc
_DW10:  rol     R16
        rol     R17
        dec     R22
        breq    _DWRet
        rol     R18
        rol     R19
        brcs    _DW20
        sub     R18, R20
        sbc     R19, R21
        brcc    _DW30
        add     R18, R20
        adc     R19, R21
        clc
        rjmp    _DW10
_DW20:  sub     R18, R20
        sbc     R19, R21
_DW30:  sec
        rjmp    _DW10
_DWCS:  sec
_DWRet: ret