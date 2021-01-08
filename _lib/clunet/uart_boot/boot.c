#include "boot.h"

#define UART_MESSAGE_CODE_CLUNET 1
#define UART_MESSAGE_CODE_DEBUG 10

const char UART_MESSAGE_PREAMBULE[] = {0xC9, 0xE7};

#define UART_TX_BUF_LENGTH SPM_PAGESIZE + 4 + 10
#define UART_RX_BUF_LENGTH SPM_PAGESIZE + 4 + 10

char uart_tx_data[UART_TX_BUF_LENGTH];
char uart_rx_data[UART_RX_BUF_LENGTH];

volatile unsigned char uart_tx_data_pos = 0;
volatile unsigned char uart_tx_data_len = 0;
char* clunet_send_buf = &uart_tx_data[4];

volatile unsigned char uart_rx_data_len = 0;
char* clunet_receive_buf = &uart_rx_data[4];

static void (*jump_to_app)(void) = 0x0000;

ISR(USART_RXC_vect){
	if (uart_rx_data_len < UART_RX_BUF_LENGTH){
		uart_rx_data[uart_rx_data_len++] = UDR;
	}
}

ISR(USART_UDRE_vect){
	if (uart_tx_data_pos < uart_tx_data_len){
		UDR = uart_tx_data[uart_tx_data_pos++];
	} else {
		//глушим прерывание по опустошению, выходим из обработчика
		unset_bit(UCSRB, UDRIE);
	}
}

volatile unsigned char systime = 0;
ISR(TIMER_COMP_VECTOR){
	++systime;
	TIMER_REG = 0;	//reset counter
}

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


char p_num = -1;				//номер текущей страницы
uint16_t p_size = 0;			//накопленный размер страницы
char p_buffer[SPM_PAGESIZE];	//буфер

static void write_flash_page(){
	eeprom_busy_wait();
	
	uint32_t p_offset = p_num * SPM_PAGESIZE;
	
	boot_page_erase(p_offset);
	boot_spm_busy_wait();      // Wait until the memory is erased.

	uint16_t* pagebuffer = (uint16_t *)p_buffer;
	
	for (uint16_t i=0; i < p_size; i+=2){
		boot_page_fill (p_offset + i, *pagebuffer++);
	}

	boot_page_write(p_offset);     	// Store buffer in flash page.
	boot_spm_busy_wait();           // Wait until the memory is written.

	boot_rww_enable ();
}

unsigned char timeout = REBOOT_TIMEOUT/TIMER_PERIOD;
unsigned char prev_systime = 0;

static signed char read(){
	if (uart_rx_data_len > 2){
		//if (uart_rx_data[0] == UART_MESSAGE_PREAMBULE[0] && uart_rx_data[1] == UART_MESSAGE_PREAMBULE[1]){
			unsigned char len = uart_rx_data[2] + 2;
			if (uart_rx_data_len >= len){
				//if (uart_rx_data_len == len){
					if (uart_rx_data[uart_rx_data_len-1] == check_crc(uart_rx_data + 2, uart_rx_data_len - 3)){
						uart_rx_data_len = 0;
						//if (clunet_receive_buf[CLUNET_OFFSET_DST_ADDRESS] == CLUNET_DEVICE_ID && clunet_receive_buf[CLUNET_OFFSET_COMMAND] == CLUNET_COMMAND_BOOT_CONTROL){
							return 1;
						//}
					}else{
						return -4;	//wrong crc
					}
				//}else{
				//	return -3; //пришло больше длины сообщения
				//}
			}
		//}else{
		//	return -2;	//неверная преамбула
		//}
	}
	
	return 0;	//сообщение не получено или получено не полностью
}

static char check_timeout(){
	if (systime != prev_systime){
		prev_systime = systime;
		if (--timeout == 0){
			return 1;
		}
	}
	
	return 0;
}

static void send(unsigned char size){
	/*
	while (test_bit(UCSRB, UDRIE)){		//wait ready to send
		if (check_timeout()){
			return 0;
		}
	}
	*/
	
	uart_tx_data[2] = 3 + size + 4;	//set uart message length
	clunet_send_buf[CLUNET_OFFSET_SIZE] = size;
	clunet_send_buf[CLUNET_OFFSET_DATA + size] = check_crc(uart_tx_data + 2, 2 + size + 4);
		
	uart_tx_data_pos = 0;
	uart_tx_data_len = 3 + size + 4 + 2;
	set_bit(UCSRB, UDRIE);
	//return 1;
}

static void send_firmware_command(uint8_t command){
	bootloader_header_t *h = ((bootloader_header_t *)&clunet_send_buf[CLUNET_OFFSET_DATA]);
	h->subcommand = command;
	
	return send(sizeof(bootloader_header_t));
}

static void firmware_update(){
		
	uint8_t state = COMMAND_FIRMWARE_UPDATE_START;
	send_firmware_command(COMMAND_FIRMWARE_UPDATE_START);
	
	while(1){
		signed char r;
		if (check_timeout()){
			r = -1;
		} else {
			r = read();
		}
		
		if (r > 0){
			timeout = NEXT_COMMAND_TIMEOUT/TIMER_PERIOD;	//reset timeout
			
			bootloader_header_t *h = ((bootloader_header_t *)&clunet_receive_buf[CLUNET_OFFSET_DATA]);
			switch(h->subcommand){
				case COMMAND_FIRMWARE_UPDATE_INIT:
					if (state == COMMAND_FIRMWARE_UPDATE_START){
						bootloader_header_ready_t *hr = ((bootloader_header_ready_t *)&clunet_send_buf[CLUNET_OFFSET_DATA]);
						hr->header.subcommand = COMMAND_FIRMWARE_UPDATE_READY;
						hr->spm_pagesize = SPM_PAGESIZE;
						
						send(sizeof(bootloader_header_ready_t));
						state = COMMAND_FIRMWARE_UPDATE_READY;
						
						break;	
					}
					return;
				case COMMAND_FIRMWARE_UPDATE_WRITE:
					if (state == COMMAND_FIRMWARE_UPDATE_READY){
						bootloader_header_data_t *h = ((bootloader_header_data_t *)&clunet_receive_buf[CLUNET_OFFSET_DATA]);
						if (p_num != h->page_num){	//новая страница
							if (p_num >= 0){
								write_flash_page();
							}
								
							p_num = h->page_num;
							p_size = 0;
						}
						for (uint16_t i=0; i<h->size; i++){
							p_buffer[p_size+i] = clunet_receive_buf[CLUNET_OFFSET_DATA + sizeof(bootloader_header_data_t) + i];
						}
						p_size += h->size;
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
		}else if (r < 0){
			return;	//ошибка получения сообщения
		}
	}
}


int main (void){
	cli();

	GICR = (1<<IVCE);	/* Enable change of interrupt vectors */
	GICR = (1<<IVSEL);  /* Move interrupts to boot flash section */
		
	uart_tx_data[0] = UART_MESSAGE_PREAMBULE[0];
	uart_tx_data[1] = UART_MESSAGE_PREAMBULE[1];
	//uart_tx_data[2] = length;
	uart_tx_data[3] = UART_MESSAGE_CODE_CLUNET;
	clunet_send_buf[CLUNET_OFFSET_SRC_ADDRESS] = CLUNET_DEVICE_ID;
	clunet_send_buf[CLUNET_OFFSET_DST_ADDRESS] = CLUNET_BROADCAST_ADDRESS;
	clunet_send_buf[CLUNET_OFFSET_COMMAND] = CLUNET_COMMAND_BOOT_CONTROL;
		
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;
	UART_INIT;

	sei();
	firmware_update();

	GICR = (1<<IVCE);	 /* Enable change of interrupt vectors */
	GICR = (0<<IVSEL);   /* Move interrupts back */
	
	jump_to_app();
	return 0;
}