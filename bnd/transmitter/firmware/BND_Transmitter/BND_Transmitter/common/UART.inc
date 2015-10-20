.equ	TxD		=PA6		;Transmit pin is PD1
.equ	b		=14			;9600 bps @ ~1 MHz crystal

.def	bitcnt	=R20		;bit counter
.def	temp	=R21		;temporary storage register

.def	Txbyte	=R22		;Data to be transmitted

.equ		sb	=1			;Number of stop bits (1, 2, ...)