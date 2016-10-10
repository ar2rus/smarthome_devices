
#include "boot.h"

char read_buffer[CLUNET_READ_BUFFER_SIZE];
char send_buffer[CLUNET_SEND_BUFFER_SIZE];

volatile char p_num = -1;				//номер текущей страницы
volatile uint16_t p_size = 0;			//накопленный размер страницы
char p_buffer[SPM_PAGESIZE];			//буфер

static void (*jump_to_app)(void) = 0x0000;

char check_crc(char* data, unsigned char size){
      uint8_t crc=0;
      uint8_t i,j;
      for (i=0; i<size;i++) {
            uint8_t inbyte = data[i];
            for (j=0;j<8;j++) {
                  uint8_t mix = (crc ^ inbyte) & 0x01;
                  crc >>= 1;
                  if (mix){
                        crc ^= 0x8C;
				  }
                  inbyte >>= 1;
            }
      }
      return crc;
}

static void write_flash_page(){
	eeprom_busy_wait();
	
	uint32_t p_offset = p_num * SPM_PAGESIZE;
	
	boot_page_erase (p_offset);
	boot_spm_busy_wait ();      // Wait until the memory is erased.

	uint16_t* pagebuffer = (uint16_t *)p_buffer;
	
	for (uint16_t i=0; i < p_size; i+=2){
		boot_page_fill (p_offset + i, *pagebuffer++);
	}

	boot_page_write(p_offset);     	// Store buffer in flash page.
	boot_spm_busy_wait();           // Wait until the memory is written.

	boot_rww_enable ();
}



static char wait_for_signal(){
	int time = 0;
	CLUNET_TIMER_REG = 0;
	while (time < ((BOOTLOADER_TIMEOUT*CLUNET_T)>>8)){
		if (CLUNET_READING) return 1;
		if (CLUNET_TIMER_REG >= 254){
			CLUNET_TIMER_REG = 0;
			time++;
		}
	}
	return 0;
}

static char read(){
	int current_byte = 0, current_bit = 0;
	do{
		if (!wait_for_signal()) return 0;
		CLUNET_TIMER_REG = 0;
		while (CLUNET_READING);
	} while (CLUNET_TIMER_REG <  (CLUNET_1_T+ CLUNET_INIT_T)/2);
	
	if (!wait_for_signal()) return 0; // Init
	while (CLUNET_READING);
	if (!wait_for_signal()) return 0;
	while (CLUNET_READING);
	current_byte = 0;
	current_bit = 0;
		
	do{
		read_buffer[current_byte] = 0;
		for (current_bit = 0; current_bit < 8; current_bit++){
			if (!wait_for_signal()) return 0;
			CLUNET_TIMER_REG = 0;
			while (CLUNET_READING);
			if (CLUNET_TIMER_REG > (CLUNET_0_T+CLUNET_1_T)/2) read_buffer[current_byte] |= (1<<current_bit);
		}
		current_byte++;
	} while (((current_byte < 4) || (current_byte < read_buffer[CLUNET_OFFSET_SIZE]+CLUNET_OFFSET_DATA+1)) && (current_byte < CLUNET_READ_BUFFER_SIZE));
	
	if ((read_buffer[CLUNET_OFFSET_DST_ADDRESS] == CLUNET_DEVICE_ID) && (check_crc(read_buffer, current_byte) == 0) && (read_buffer[CLUNET_OFFSET_COMMAND] == CLUNET_COMMAND_BOOT_CONTROL)){
		return read_buffer[CLUNET_OFFSET_SIZE];
	}
	return -1; // Пришёл пакет, но левый
}

static void send(unsigned char size){
	
	send_buffer[CLUNET_OFFSET_SIZE] =  size;
	
	size += CLUNET_OFFSET_DATA;
	
	CLUNET_SEND_0;
	pause(3);
	send_bit(10);				// Init
	send_bit(3); send_bit(3);	// Prio
	
	char crc = check_crc(send_buffer, size);
	for(uint8_t i = 0; i <= size; i++){
		char b = (i < size) ? send_buffer[i] : crc;
		for (uint8_t m = 0; m < 8; m++){
			CLUNET_SEND_1;
			char p = (b & (1<<m)) ? 3 : 1;
			pause(p); CLUNET_SEND_0; pause(1);
		}
	}
}

void send_firmware_command(uint8_t command){
	bootloader_header_t *h = ((bootloader_header_t *)&send_buffer[CLUNET_OFFSET_DATA]);
	h->subcommand = command;
	
	send(sizeof(bootloader_header_t));
}


static void firmware_update(){
	
	uint8_t state = COMMAND_FIRMWARE_UPDATE_START;
	send_firmware_command(COMMAND_FIRMWARE_UPDATE_START);
	
	char num_attempts;
	
	while(1){
		if (read() > 0){
			num_attempts = BOOTLOADER_WAIT_ATTEMPTS;
			
			bootloader_header_t *h = ((bootloader_header_t *)&read_buffer[CLUNET_OFFSET_DATA]);
			switch(h->subcommand){
				case COMMAND_FIRMWARE_UPDATE_INIT:
					if (state == COMMAND_FIRMWARE_UPDATE_START){
						bootloader_header_ready_t *hr = ((bootloader_header_ready_t *)&send_buffer[CLUNET_OFFSET_DATA]);
						hr->header.subcommand = COMMAND_FIRMWARE_UPDATE_READY;
						
						hr->spm_pagesize = SPM_PAGESIZE;
						
						send(sizeof(bootloader_header_ready_t));
						state = COMMAND_FIRMWARE_UPDATE_READY;
						break;
					}
					return;
				case COMMAND_FIRMWARE_UPDATE_WRITE:
					if (state == COMMAND_FIRMWARE_UPDATE_READY){
						bootloader_header_data_t *h = ((bootloader_header_data_t *)&read_buffer[CLUNET_OFFSET_DATA]);
						if (p_num != h->page_num){	//новая страница
							if (p_num >= 0){
								write_flash_page();
							}
								
							p_num = h->page_num;
							p_size = 0;
						}
						for (uint16_t i=0; i<h->size; i++){
							p_buffer[p_size++] = read_buffer[CLUNET_OFFSET_DATA + sizeof(bootloader_header_data_t) + i];
						}
						send_firmware_command(COMMAND_FIRMWARE_UPDATE_WRITTEN);
						break;
					}
					return;
				case COMMAND_FIRMWARE_UPDATE_DONE:
					if (p_size > 0){	//записываем остатки (неполная страница)
						write_flash_page();
					}
					return;
			}
		}else{
			if (state == COMMAND_FIRMWARE_UPDATE_START){	//ожидание загрузки бутлоадера = 1 таймаут
				return;
			}else{
				if (--num_attempts <= 0){					//ожидание очередной стадии передачи бутлоадера = BOOTLOADER_WAIT_ATTEMPTS таймаутов
					send_firmware_command(COMMAND_FIRMWARE_UPDATE_ERROR);
					return;
				}
			}
		}
	}
}


int main (void){
	cli();
	
	//init send buffer
	send_buffer[CLUNET_OFFSET_SRC_ADDRESS] = CLUNET_DEVICE_ID;
	send_buffer[CLUNET_OFFSET_DST_ADDRESS] = CLUNET_BROADCAST_ADDRESS;
	send_buffer[CLUNET_OFFSET_COMMAND]	   = CLUNET_COMMAND_BOOT_CONTROL;
	
	//init clunet
	CLUNET_TIMER_INIT;
	CLUNET_READ_INIT;
	CLUNET_SEND_INIT;
	
	firmware_update();

	jump_to_app();
	return 0;
}