NRF24L01_Init:
	in r16, NRF24_DDR
	ori r16, 1<<NRF24_MOSI|1<<NRF24_SCK|1<<NRF24_CSN|1<<NRF24_CE
	andi r16, ~(1<<NRF24_MISO)
	out NRF24_DDR, r16

	in r16, NRF24_PORT
	ori r16, 1<<NRF24_CSN
	andi r16, ~(1<<NRF24_CE)
	out NRF24_PORT, r16

	;delay 5ms
	ldi r16, 4
	sts delay_counter, r16
	rcall delay_1024mks

	;reset nrf24_reg_length
	ldi r16, 1
	sts nrf24_reg_length, r16

	;write CONFIG register
	;CRC - 2 bytes, CRC enabled, MAX_RT irq disabled, TX_DS irq disabled, RX_DR irq disabled
	ldi r16, 1<<NRF24_REGISTER_CONFIG_CRCO|1<<NRF24_REGISTER_CONFIG_EN_CRC|1<<NRF24_REGISTER_CONFIG_MASK_MAX_RT|1<<NRF24_REGISTER_CONFIG_MASK_TX_DS|1<<NRF24_REGISTER_CONFIG_MASK_RX_DR
	sts nrf24_spi_buffer_tx, r16
	rcall NRF24L01_WriteConfig

	;write SETUP_AW register
	;5 byte address
	ldi r16, NRF24_REGISTER_SETUP_AW
	sts nrf24_reg_command, r16
	ldi r16, 0x03				
	sts nrf24_spi_buffer_tx, r16
	rcall NRF24L01_WRegister

	;write RF_CH_REGISTER register
	;85 channel
	ldi r16, NRF24_REGISTER_RF_CH
	sts nrf24_reg_command, r16
	ldi r16, 85
	sts nrf24_spi_buffer_tx, r16
	rcall NRF24L01_WRegister

	;write RF_SETUP_REGISTER register
	;LNA gain, 0 dBm, 2 Mbps (most reliable)
	ldi r16, NRF24_REGISTER_RF_SETUP
	sts nrf24_reg_command, r16
	ldi r16, 1<<NRF24_REGISTER_RF_SETUP_LNA_HCURR|1<<NRF24_REGISTER_RF_SETUP_RF_PWR_0|1<<NRF24_REGISTER_RF_SETUP_RF_PWR_1|1<<NRF24_REGISTER_RF_SETUP_RF_DR_HIGH|0<<NRF24_REGISTER_RF_SETUP_RF_DR_LOW
	sts nrf24_spi_buffer_tx, r16
	rcall NRF24L01_WRegister

	;write RX_ADDR_P0
	;set pipe0 recieve address 0xA1A1A1A1A1
	ldi r16, NRF24_REGISTER_RX_ADDR_P0
	sts nrf24_reg_command, r16

	ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 0, r16
	;ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 1, r16
	;ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 2, r16
	;ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 3, r16
	;ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 4, r16
	
	ldi r16, 5
	sts nrf24_reg_length, r16
	rcall NRF24L01_WRegister

	;write TX_ADDR - the same as RX_ADDR_P0 
	;set transmit address 0xA1A1A1A1A1
	ldi r16, NRF24_REGISTER_TX_ADDR
	sts nrf24_reg_command, r16
	ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 0, r16
	;ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 1, r16
	;ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 2, r16
	;ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 3, r16
	;ldi r16, 0xA1
	sts nrf24_spi_buffer_tx + 4, r16
	
	ldi r16, 5
	sts nrf24_reg_length, r16
	rcall NRF24L01_WRegister

	rcall NRF24L01_ResetIRQs
	rcall NRF24L01_FlushRX
	rcall NRF24L01_FlushTX

	ret

/*
NRF24L01_Disable:
	in r16, NRF24_PORT
	andi r16, ~(1<<NRF24_MOSI|1<<NRF24_SCK|1<<NRF24_CSN|1<<NRF24_CE)
	out NRF24_PORT, r16

	ret
*/

;setup module as transmitter
NRF24L01_StartWrite:
	
	rcall NRF24L01_ReadConfig
	lds r16, nrf24_spi_buffer_rx
	ori r16,  1<<NRF24_REGISTER_CONFIG_PWR_UP
	andi r16, ~(1<<NRF24_REGISTER_CONFIG_PRIM_RX)
	sts nrf24_spi_buffer_tx, r16
	rcall NRF24L01_WriteConfig

	/*
	;delay 1ms (needed 150 mkS)
	ldi r16, 0
	sts delay_counter, r16
	rcall delay_1024mks
	*/

	ldi r16, 75					;delay 150+ mks
	sts delay_counter, r16
	rcall delay_3mks

	rcall NRF24L01_ResetIRQs
	rcall NRF24L01_FlushTX
		
	ret

 NRF24L01_Write:
	ldi r16, NRF24_W_TX_PAYLOAD
	sts nrf24_reg_command, r16
	ldi r16, RX_PW_P0_LEN
	sts nrf24_reg_length, r16
	rcall NRF24L01_WriteCommand

	sbi NRF24_PORT, NRF24_CE

	/*;delay 25 mks (needed 15 mkS)
	ldi r16, 0
	delay_25mks:
	subi r16, 1
	brne delay_25mks*/

	ldi r16, 10					;delay 25+ mks
	sts delay_counter, r16
	rcall delay_3mks

	cbi NRF24_PORT, NRF24_CE

	;wait finish of operation
	ldi r17, 255
	nrf24WaitSending:
		;check
		rcall NRF24L01_ReadStatus
		lds r16, nrf24_spi_buffer_rx
		andi r16, 1<<NRF24_REGISTER_STATUS_MAX_RT|1<<NRF24_REGISTER_STATUS_TX_DS
		brne nrf24SendingFinished

		ldi r16, 100				;timeout between check attempts (300 mks)
		sts delay_counter, r16
		rcall delay_3mks

		dec r17
		brne nrf24WaitSending

	nrf24SendingFinished:
	ret

/*;setup module as reciever
NRF24L01_StartListening:
	;write CONFIG register
	;PRX, power up
	rcall NRF24L01_ReadConfig
	lds r16, nrf24_spi_buffer_rx
	ori r16,  1<<NRF24_REGISTER_CONFIG_PRIM_RX|1<<NRF24_REGISTER_CONFIG_PWR_UP
	sts nrf24_spi_buffer_tx, r16
	rcall NRF24L01_WriteConfig

	rcall NRF24L01_ResetIRQs
	
	rcall NRF24L01_FlushRX
	rcall NRF24L01_FlushTX


	;write RX_PW_P0
	;set number of bytes to recieve
	ldi r16, RX_PW_P0_LEN				;изменить на значение из протокола
	sts nrf24_spi_buffer_tx, r16
	ldi r16, NRF24_REGISTER_RX_PW_P0
	sts nrf24_reg_command, r16
	rcall NRF24L01_WRegister

	sbi NRF24_PORT, NRF24_CE

	;delay 1ms (needed 13 mkS)
	ldi r16, 0
	sts delay_cnt, r16
	rcall delay_1024mks

	ret

;setup module as reciever
NRF24L01_StopListening:
	cbi NRF24_PORT, NRF24_CE
	rcall NRF24L01_FlushRX
	rcall NRF24L01_FlushTX
	ret*/

;power module down
NRF24L01_PowerDown:
	rcall NRF24L01_ReadConfig
	lds r16, nrf24_spi_buffer_rx
	andi r16,  ~(1<<NRF24_REGISTER_CONFIG_PWR_UP)
	sts nrf24_spi_buffer_tx, r16
	rcall NRF24L01_WriteConfig
	ret
	
/*;power module up
NRF24L01_PowerUp:
	rcall NRF24L01_ReadConfig
	lds r16, nrf24_spi_buffer_rx
	ori r16,  1<<NRF24_REGISTER_CONFIG_PWR_UP
	sts nrf24_spi_buffer_tx, r16
	rcall NRF24L01_WriteConfig
	ret*/


;write CONFIG register
;data byte in nrf24_spi_buffer_tx
NRF24L01_WriteConfig:
	ldi r16, NRF24_REGISTER_CONFIG
	sts nrf24_reg_command, r16
	rcall NRF24L01_WRegister
	ret

;reset irq's by writing STATUS register
NRF24L01_ResetIRQs:
	ldi r16, 1<<NRF24_REGISTER_STATUS_MAX_RT|1<<NRF24_REGISTER_STATUS_TX_DS|1<<NRF24_REGISTER_STATUS_RX_DR
	sts nrf24_spi_buffer_tx, r16
	rcall NRF24L01_WriteStatus
	ret

;write STATUS register
;data byte in nrf24_spi_buffer_tx
NRF24L01_WriteStatus:
	ldi r16, NRF24_REGISTER_STATUS
	sts nrf24_reg_command, r16
	rcall NRF24L01_WRegister
	ret
	
;read CONFIG register
;data byte in nrf24_spi_buffer_rx
NRF24L01_ReadConfig:
	ldi r16, NRF24_REGISTER_CONFIG
	sts nrf24_reg_command, r16
	rcall NRF24L01_RRegister
	ret

;read STATUS register
;data byte in nrf24_spi_buffer_rx
NRF24L01_ReadStatus:
	ldi r16, NRF24_REGISTER_STATUS
	sts nrf24_reg_command, r16
	rcall NRF24L01_RRegister
	ret

/*;read FIFO_STATUS register
;data byte in nrf24_spi_buffer_rx
NRF24L01_ReadFifoStatus:
	ldi r16, NRF24_REGISTER_FIFO_STATUS
	sts nrf24_reg_command, r16
	rcall NRF24L01_RRegister
	ret*/


;clear tx buffer using FLUSH_TX instruction
NRF24L01_FlushTX:
	ldi r16, NRF24_FLUSH_TX
	sts nrf24_reg_command, r16
	ldi r16, 0
	sts nrf24_reg_length, r16
	rcall NRF24L01_WriteCommand
	ret

;clear rx buffer using FLUSH_RX instruction
NRF24L01_FlushRX:
	ldi r16, NRF24_FLUSH_RX
	sts nrf24_reg_command, r16
	ldi r16, 0
	sts nrf24_reg_length, r16
	rcall NRF24L01_WriteCommand
	ret
		

;write register, using W_REGISTER instruction
;register address in nrf24_reg_command
;data in nrf24_spi_buffer_tx
;data length in nrf24_reg_length (default = 1)
NRF24L01_WRegister:
	lds r16, nrf24_reg_command
	ori r16, NRF24_W_REGISTER
	sts nrf24_reg_command, r16
	rcall NRF24L01_WriteCommand
	ret
	
;read register, using R_REGISTER instruction
;register address in nrf24_reg_command
;data puts to nrf24_spi_buffer_rx
;data length in nrf24_reg_length
NRF24L01_RRegister:
	lds r16, nrf24_reg_command
	ori r16, NRF24_R_REGISTER
	sts nrf24_reg_command, r16
	rcall NRF24L01_ReadCommand
	ret

;send command to write data
;register address in nrf24_reg_command
;data in nrf24_spi_buffer_tx
;data length in nrf24_reg_length (default = 1)
NRF24L01_WriteCommand:
	push r16
	push r17
	
	cbi NRF24_PORT, NRF24_CSN

	lds r16, nrf24_reg_command
	sts nrf24_spi_byte_tx, r16
	rcall SPI_SendByte

	ldi XL, low(nrf24_spi_buffer_tx)
	;ldi XH, high(nrf24_spi_buffer_tx)
	lds r16, nrf24_reg_length
	cpi r16, 0x00
	breq NRF24L01_WriteCommandEnd
	
	NRF24L01_WriteCommandLoop:
		;rcall delay_n
		ld r17, X+
		sts nrf24_spi_byte_tx, r17
		rcall SPI_SendByte
		dec r16
		brne NRF24L01_WriteCommandLoop

	NRF24L01_WriteCommandEnd:
		ldi r16, 0x01 			;set default data length=1
		sts nrf24_reg_length, r16
	
		sbi NRF24_PORT, NRF24_CSN
	
	pop r17
	pop r16
	ret
	
;send command to read data
;register address in nrf24_reg_command
;data puts to nrf24_spi_buffer_rx
;data length for reading in nrf24_reg_length (default = 1)
NRF24L01_ReadCommand:
	push r16
	
	cbi NRF24_PORT, NRF24_CSN

	lds r16, nrf24_reg_command
	sts nrf24_spi_byte_tx, r16
	rcall SPI_SendByte

	ldi XL, low(nrf24_spi_buffer_rx)
	;ldi XH, high(nrf24_spi_buffer_rx)
	lds r16, nrf24_reg_length
	
	NRF24L01_ReadCommandLoop:
		;rcall delay_n
		rcall SPI_RecieveByte
		push r16
		lds r16, nrf24_spi_byte_rx
		st X+, r16
		pop r16
		dec r16
		brne NRF24L01_ReadCommandLoop

	ldi r16, 0x01 			;set default data length=1
	
	sts nrf24_reg_length, r16
	sbi NRF24_PORT, NRF24_CSN
	
	pop r16
	ret
	
	
	
	
/*****SPI*****/	

;SCK strobe
SPI_SCKStrobe:
	sbi NRF24_PORT, NRF24_SCK
	;rcall delay_n
	cbi NRF24_PORT, NRF24_SCK
	ret

;отправка байта данных nrf24_spi_byte_tx по SPI старшим битом вперед
SPI_SendByte:
	push r16
	push r17

	lds r16, nrf24_spi_byte_tx
	ldi r17, 8
	
	SPI_SendByteLoop:
		;rcall delay_n
		sbrs r16, 7
		rjmp PC + 3 
		sbi NRF24_PORT, NRF24_MOSI
		rjmp PC + 2
		cbi NRF24_PORT, NRF24_MOSI
		
		rcall SPI_SCKStrobe
		lsl r16
  		dec r17
		brne SPI_SendByteLoop

	pop r17
	pop r16
	ret
	
;чтение байта nrf24_spi_byte_rx данных по SPI
SPI_RecieveByte:
	push r16
	push r17
	ldi r16, 8
	clr r17

  SPI_RecieveByteLoop:
	;rcall delay_n
	lsl r17
	sbic NRF24_PIN, NRF24_MISO
	ori r17, 0x01

	rcall SPI_SCKStrobe
	dec r16
	brne SPI_RecieveByteLoop

	sts nrf24_spi_byte_rx, r17

	pop r17
	pop r16
	ret