#include "Bridge.h"

volatile unsigned char systime = 0;
unsigned char prev_systime = 0;

unsigned int second_counter = 0;

volatile display_t display;

void display_update(){
	switch (display.mode){
		case OFF:
			SIGNS_OFF;
			break;
		case NUMBERS:
			if (display.sign){
				unsigned char d = display.number / 10;
				SIGN1(CODE((d ? SYMBOL_DIGIT[d] : 0), display.led_on));
			}else{
				SIGN0(CODE(SYMBOL_DIGIT[display.number % 10], display.led_on));
			}
			break;
		case DASHES:
			if (display.sign){
				SIGN1(CODE(SYMBOL_DASH, display.led_on));
			}else{
				SIGN0(CODE(SYMBOL_DASH, display.led_on));
			}
			break;
	}
	display.sign = !display.sign;
}


#define DISCOVERY_OBSERVE_PERIOD 2000	//ms
#define DISCOVERY_SHOW_PERIOD 10	    //s

signed int discovery_observe_time = 0;
signed int discovery_show_time = 0;
unsigned char discovery_responses_count = 0;

void discovery_broadcast(){
	while(clunet_ready_to_send());
	clunet_send_fake(0x00, CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_DISCOVERY, 0, 0);
}

void discovery_listen(clunet_msg* msg){
	if (msg->dst_address == CLUNET_BROADCAST_ADDRESS && msg->command == CLUNET_COMMAND_DISCOVERY){
		discovery_responses_count = 0;
		discovery_observe_time = DISCOVERY_OBSERVE_PERIOD;
		discovery_show_time = DISCOVERY_SHOW_PERIOD;
	}

	if (discovery_observe_time){
		if (msg->command == CLUNET_COMMAND_DISCOVERY_RESPONSE){
			discovery_responses_count++;
		}
	}
}


ISR(TIMER_COMP_VECTOR){
	++systime;
	TIMER_REG = 0;	//reset counter
	
	display_update();
}


const char UART_MESSAGE_PREAMBULE[] = {0xC9, 0xE7};

#define UART_RX_BUF_LENGTH 78	//CLUNET_BUFFERED_DATA_MAX_LENGTH + 4 + 10
volatile char uart_rx_data[UART_RX_BUF_LENGTH];
volatile unsigned char uart_rx_data_len = 0;

ISR(USART_RXC_vect){
	if (uart_rx_data_len < UART_RX_BUF_LENGTH){
		uart_rx_data[uart_rx_data_len++] = UDR;
	}
}
	
#define UART_TX_BUF_LENGTH 78	//CLUNET_BUFFERED_DATA_MAX_LENGTH + 4 + 10 
volatile char uart_tx_data[UART_TX_BUF_LENGTH];	
volatile unsigned char uart_tx_data_pos = 0;
volatile unsigned char uart_tx_data_len = 0;

ISR(USART_UDRE_vect){
	if (uart_tx_data_pos < uart_tx_data_len){
		UDR = uart_tx_data[uart_tx_data_pos++];
	} else {
		//глушим прерывание по опустошению, выходим из обработчика
		unset_bit(UCSRB, UDRIE);
	}
}

char uart_ready_to_send(){
	return !test_bit(UCSRB, UDRIE);
}

char uart_add_byte_to_send(char byte){
	if (uart_ready_to_send() && (uart_tx_data_len + 1) < UART_TX_BUF_LENGTH){
		uart_tx_data[uart_tx_data_len++] = byte; 
		return uart_tx_data_len;
	}
	return 0;
}

char uart_add_bytes_to_send(char* bytes, unsigned char length){
	if (uart_ready_to_send() && length && (uart_tx_data_len + length) < UART_TX_BUF_LENGTH){
		memcpy((void*)(uart_tx_data + uart_tx_data_len), bytes, length);
		uart_tx_data_len += length;
		return uart_tx_data_len;
	}
	return 0;
}

char uart_add_crc_to_send(unsigned char buffer_start_offset){
	if (buffer_start_offset < uart_tx_data_len){
		return uart_add_byte_to_send(check_crc((char*)(uart_tx_data + buffer_start_offset), uart_tx_data_len - buffer_start_offset));
	}
	return 0;
}

char uart_send(){
	if (uart_ready_to_send() && uart_tx_data_len){
		uart_tx_data_pos = 0;
		set_bit(UCSRB, UDRIE);
		
		return uart_tx_data_len;
	}
	return 0;
}

#define UART_MESSAGE_CODE_CLUNET 1
#define UART_MESSAGE_CODE_DEBUG 10

char uart_send_message(char code, char* data, unsigned char length){
	if (uart_ready_to_send()){
		uart_tx_data_len = 0;
		uart_add_bytes_to_send((char*)UART_MESSAGE_PREAMBULE, 2);	//preambule
		uart_add_byte_to_send(length + 3);							//length
		uart_add_byte_to_send(code);								//code
		uart_add_bytes_to_send(data, length);						//message
		uart_add_crc_to_send(2);									//crc (without preambule)
		
		return uart_send();
	}
	return 0;
}

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	if (!CLUNET_MULTICAST_DEVICE(src_address)){
		clunet_buffered_push(src_address, dst_address, command, data, size);
	}
}

char button_value;

void analyze_uart_rx_trim(unsigned char offset){
	if (offset <= uart_rx_data_len){
		uart_rx_data_len -= offset;
		if (uart_rx_data_len){
			memmove((void*)uart_rx_data, (void*)(uart_rx_data + offset), uart_rx_data_len);
		}
	}
}

void analyze_uart_rx(void(*f)(unsigned char code, char* data, unsigned char length)){
	while (uart_rx_data_len > 1){
		unsigned char uart_rx_preambula_offset = uart_rx_data_len - 1;	//первый байт преамбулы может быть прочитан, а второй еще не пришел
		for (unsigned char i=0; i < uart_rx_data_len - 1; i++){
			if (uart_rx_data[i+0] == UART_MESSAGE_PREAMBULE[0] && uart_rx_data[i+1] == UART_MESSAGE_PREAMBULE[1]){
				uart_rx_preambula_offset = i;
				break;
			}
		}
		if (uart_rx_preambula_offset) {
			analyze_uart_rx_trim(uart_rx_preambula_offset); //обрезаем мусор до преамбулы
		}

		if (uart_rx_data_len >= 5){	//минимальная длина сообщения с преамбулой
			char* uart_rx_message = (char*)(uart_rx_data + 2);
			unsigned char length = uart_rx_message[0];
			if (length < 3 || length > UART_RX_BUF_LENGTH){
				analyze_uart_rx_trim(2); 	//пришел мусор, отрезаем преамбулу и надо пробовать искать преамбулу снова
				continue;
			}
					
			if (uart_rx_data_len >= length+2){		//в буфере данных уже столько, сколько описано в поле length
				if (check_crc(uart_rx_message, length - 1) == uart_rx_message[length - 1]){ //проверка crc
					if (f){
						f(uart_rx_message[1], &uart_rx_message[2], length - 3);
					}					
					analyze_uart_rx_trim(length+2); //отрезаем прочитанное сообщение
				}else{
					analyze_uart_rx_trim(2); 
				}
			}else{
				break;
			}
		}else{
			break;
		}
	}
}

void on_uart_message(unsigned char code, char* data, unsigned char length){
	switch(code){
		case UART_MESSAGE_CODE_CLUNET:
			if (data && length >= 4){
				clunet_msg* msg = (clunet_msg*)data;
				if (CLUNET_MULTICAST_DEVICE(msg->src_address)){
					//TODO: move to buffer at first
					
					/*	
						if (msg->command == CLUNET_COMMAND_REBOOT){ // Просто ребут. И да, ребутнуть себя мы можем
						clunet_data_received(0x01,0xEE,0x2A,0,0);
							//cli();
							//set_bit(WDTCR, WDE);
							//while(1);
						}
					*/
					discovery_listen(msg);
					while(clunet_ready_to_send());
					clunet_send_fake(msg->src_address, msg->dst_address, 0, msg->command, msg->data, msg->size);
				}
			}
			break;
		case UART_MESSAGE_CODE_DEBUG:
		break;
	}
}

int main(void){
	cli();
	
	TIMER_INIT;
	ENABLE_TIMER_CMP_A;	//main loop timer 1ms
	
	DISPLAY_INIT;
	display.number = 0;
	display.mode = DASHES;

	BUTTON_INIT;
	button_value = BUTTON_READ;
	
	UBRRL=25;	//38400 at 16MHz
	UCSRB=(1<<TXEN)|(1<<RXEN)|(1<<RXCIE)|(0<<UDRIE);
	UCSRC=(1<<URSEL)|(1<<UCSZ0)|(1<<UCSZ1);

	clunet_set_on_data_received_sniff(clunet_data_received);
	clunet_buffered_init();
	clunet_init();
	
	while(1){
		display.led_on = CLUNET_SENDING | CLUNET_READING;
		if (!clunet_buffered_is_empty() && uart_ready_to_send()){
			clunet_msg* msg = clunet_buffered_pop();
			discovery_listen(msg);
			uart_send_message(UART_MESSAGE_CODE_CLUNET, (char*)msg, 4 + msg->size);
		}

		analyze_uart_rx(on_uart_message);
			
		if (prev_systime != systime){
			
			unsigned int delta_ms_time = TIMER_PERIOD*(unsigned char)(systime - prev_systime);
			prev_systime = systime;
			
			if (discovery_observe_time){
				discovery_observe_time -= delta_ms_time;
			}
			
			second_counter += delta_ms_time;
			if (second_counter > 1000){
				second_counter -= 1000;
				
				if (discovery_show_time){
					discovery_show_time--;
				}
			}
			
			if (discovery_show_time){
				display.mode = NUMBERS;
				display.number = discovery_responses_count;
			}else{
				display.mode = DASHES;
			}
			
			char new_button_value = BUTTON_READ;
			if (new_button_value != button_value){
				if (!new_button_value){
					discovery_broadcast();
				}
				button_value = new_button_value;
			}
			
		}
	}
	
	return 0;
}