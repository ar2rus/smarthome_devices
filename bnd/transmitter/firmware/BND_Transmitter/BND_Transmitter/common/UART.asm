UART_putchar:	
		ldi	bitcnt, 9+sb	;1+8+sb (sb is # of stop bits)
		com	Txbyte			;Inverte everything
		sec					;Start bit

putchar0:	
		brcc putchar1		;If carry set
		cbi	PORTA,TxD		;send a '0'
		rjmp putchar2		;else	

putchar1:	
		sbi	PORTA,TxD		;send a '1'
		nop

putchar2:
		rcall UART_delay	;One bit delay
		rcall UART_delay

		lsr	Txbyte			;Get next bit
		dec	bitcnt			;If not all bit sent
		brne putchar0		;send next
		ret					;return


UART_delay:	
		ldi	temp,b			
UART_delay1:
		dec	temp
		brne UART_delay1
		ret