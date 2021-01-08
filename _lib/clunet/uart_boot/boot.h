/*
 * bootloader.h
 *
 * Created: 24.06.2020
 *  Author: gargon
 */ 

#ifndef BOOTLOADER_H_
#define BOOTLOADER_H_

#include "../clunet.h"

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>

typedef struct bootloader_header{
	uint8_t subcommand;
} bootloader_header_t;

typedef struct bootloader_header_ready{
	bootloader_header_t header;
	uint16_t spm_pagesize;
} bootloader_header_ready_t;

typedef struct bootloader_header_data{
	bootloader_header_t header;
	uint8_t page_num;
	uint16_t size;
} bootloader_header_data_t;


#define APP_END (FLASHEND - (BOOTSIZE * 2))

#define COMMAND_FIRMWARE_UPDATE_START 0
#define COMMAND_FIRMWARE_UPDATE_INIT 1
#define COMMAND_FIRMWARE_UPDATE_READY 2
#define COMMAND_FIRMWARE_UPDATE_WRITE 3
#define COMMAND_FIRMWARE_UPDATE_WRITTEN 4
#define COMMAND_FIRMWARE_UPDATE_DONE 5
#define COMMAND_FIRMWARE_UPDATE_ERROR 255

/* main timer controls*/
#define TIMER_PRESCALER 64
#define TIMER_PERIOD 10	//10ms
#define TIMER_NUM_TICKS (unsigned int)(TIMER_PERIOD *1e-3 * F_CPU / TIMER_PRESCALER)
#define TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = TIMER_NUM_TICKS; set_bit2(TCCR1B, CS11, CS10); unset_bit(TCCR1B, CS12);} //64x prescaler

#define ENABLE_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define TIMER_REG TCNT1
#define TIMER_COMP_VECTOR TIMER1_COMPA_vect


#define UART_INIT {UBRRL=25; UCSRB=(1<<TXEN)|(1<<RXEN)|(1<<RXCIE)|(0<<UDRIE); UCSRC=(1<<URSEL)|(1<<UCSZ0)|(1<<UCSZ1);}	//25 -> 38400 at 16MHz
#define REBOOT_TIMEOUT 500	//ms
#define NEXT_COMMAND_TIMEOUT 2000	//ms


#endif /* BOOTLOADER_H_ */