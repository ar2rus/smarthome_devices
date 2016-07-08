/*
 * bootloader.h
 *
 * Created: 20.01.2015 11:01:11
 *  Author: gargon
 */ 


#ifndef BOOTLOADER_H_
#define BOOTLOADER_H_

#include "defines.h"
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


static void pause(uint8_t t){
	CLUNET_TIMER_REG = 0; 
	while(CLUNET_TIMER_REG < (t*CLUNET_T));
}

static void send_bit(uint8_t t){
	CLUNET_SEND_1; 
	pause(t); 
	CLUNET_SEND_0; 
	pause(1);
}

void bootloader();


#endif /* BOOTLOADER_H_ */