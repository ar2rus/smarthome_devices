;wait for a second before call this subroutine
;call not more often 1 time per 2 seconds
DHTRead:
	push r16
	push r17
	push r18
	push r19

	clr   r29						;clear high Y byte
	ldi   r28, DHT22_data_valid		;set low Y byte to DHT22_valid pointer

	clr r16							;clear result data
	ldi r17, 6
	DHTClearData:
	st Y+, r16
	dec r17
	brne DHTClearData
	
	;start reading
	clt						;use T flag for store last state
	clr r19					;counter for number of bits
	ldi r17, 40				;iterator 40 - 0 (40 bit data)
	
	ldi r28, DHT22_data		;set low Y byte to dht_data pointer

	sbi DHT22_DDR, DHT22_P	;OUT
	//sbi DHT_PORT, DHTP 	;set high
	
	//ldi t1, 0
	//sts delay_cnt, t1		;delay 250ms
	//rcall delay_273ms

	cbi DHT22_PORT, DHT22_P ;set low

	ldi r16, 25
	sts delay_counter, r16	;delay 20ms
	rcall delay_1024mks
	
	sbi DHT22_PORT, DHT22_P	;set high

	//delete this if, code below works as properly
/*	ldi r16, 25				;delay 40 mks
	DHT_delay_30mks:
	subi r16, 1
	brne DHT_delay_30mks*/

	ldi r16, 25					;delay 40+ mks
	sts delay_counter, r16
	rcall delay_3mks

	clr r16
	
	cbi DHT22_DDR, DHT22_P	;INPUT
	sbi DHT22_PORT, DHT22_P	;pull up
	
	;wait dht present bit
	clr r18
	DHT_wait00:
		inc r18
		breq DHT_ready
		sbic DHT22_PIN, DHT22_P
	rjmp DHT_wait00

	;skip first bit
	clr r18
	DHT_wait1:
		inc r18
		breq DHT_ready
		sbis DHT22_PIN, DHT22_P
	rjmp DHT_wait1

	clr r18
	DHT_wait0:
		inc r18
		breq DHT_ready
		sbic DHT22_PIN, DHT22_P
	rjmp DHT_wait0

	;start of reading
	DHT_read_bit:
		clr r18
		DHT_r:
			sbic DHT22_PIN, DHT22_P
			rjmp DHT_got1
			
			brts DHT_skip1
			rjmp DHT_next
			DHT_skip1:
			clt
			rjmp DHT_analyze
			
			DHT_got1:
			brtc DHT_skip2
			rjmp DHT_next
			DHT_skip2:
			clr r18
			set
			
			DHT_next:
			inc r18
			breq DHT_ready
			rjmp DHT_r
		
		DHT_analyze:
			lsl r16
			cpi r18, DHT22_MAXCOUNT0
			brlo DHT_skip3
				ori r16, 1
			DHT_skip3:
			inc r19
			cpi r19, 8
			brlo DHT_skip4
				clr r19
				st Y+, r16
	DHT_skip4:

	dec r17
	brne DHT_read_bit

	DHT_ready:
		cpi r17, 0				;check iterator (t2)
		brne DHT_exit
		    
		ldi r28, DHT22_data		;check checksum
		ldi r18, 4
		DHT_crc:
		ld r16, Y+
		add r17, r16
		dec r18
		brne DHT_crc
		ld r16, Y
		cp r17, r16
		brne DHT_exit
		ldi r16, 1
		sts DHT22_data_valid, r16
		
	DHT_exit:
		pop r19
		pop r18
		pop r17
		pop r16
ret