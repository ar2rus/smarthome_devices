#include "Bridge.h"
extern volatile unsigned char clunetSendingState;

volatile unsigned char systime = 0;
unsigned char prev_systime = 0;

unsigned int second_counter = 0;

volatile display_t display;
extern volatile unsigned int clunet_queue_drops;

void display_update(){
	switch (display.mode){
		case OFF:
			SIGNS_OFF;
			break;
		case NUMBERS:
			if (display.sign){
				unsigned char d = (unsigned char)display.number / 10 % 10;
				SIGN1(CODE((d ? SYMBOL_DIGIT[d] : 0), display.led_on));
			}else{
				SIGN0(CODE(SYMBOL_DIGIT[(unsigned char)display.number % 10], display.led_on));
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
	if (clunetSendingState != CLUNET_SENDING_STATE_IDLE) return;
	clunet_try_send_fake(0x00, CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_DISCOVERY, 0, 0);
}

void discovery_listen_header(unsigned char dst, unsigned char command){
	if (dst == CLUNET_BROADCAST_ADDRESS && command == CLUNET_COMMAND_DISCOVERY){
		discovery_responses_count = 0;
		discovery_observe_time = DISCOVERY_OBSERVE_PERIOD;
		discovery_show_time = DISCOVERY_SHOW_PERIOD;
	}

	if (discovery_observe_time){
		if (command == CLUNET_COMMAND_DISCOVERY_RESPONSE){
			discovery_responses_count++;
		}
	}
}


void discovery_listen(clunet_msg* msg) { discovery_listen_header(msg->dst_address, msg->command); }

ISR(TIMER_COMP_VECTOR){
	++systime;
	TIMER_REG = 0;	//reset counter
	
	display_update();
}


const char UART_MESSAGE_PREAMBULE[] = {0xC9, 0xE7};

#define UART_RX_BUF_LENGTH 96	//ATmega8 bootloader page write frame is 77 bytes over UART; keep margin for resync
volatile char uart_rx_data[UART_RX_BUF_LENGTH];
volatile unsigned char uart_rx_data_len = 0;
volatile unsigned char uart_rx_overflow = 0;
volatile unsigned char uart_rx_last_at = 0;
volatile unsigned int uart_hardware_errors = 0;
volatile unsigned int uart_overflows = 0, uart_crc_errors = 0;
static unsigned int legacy_expired = 0;
extern unsigned char __bss_end;
static unsigned char* watermark_end;
static unsigned int stack_min_free = 0xFFFF;
static void init_stack_watermark(){
    watermark_end = (unsigned char*)(SP - 16);
    for (unsigned char* p = &__bss_end; p < watermark_end; ++p) *p = 0xA5;
}
static unsigned int stack_watermark(){
    unsigned char* p = &__bss_end;
    while (p < watermark_end && *p == 0xA5) ++p;
    unsigned int free_bytes = p - &__bss_end;
    if (free_bytes < stack_min_free) stack_min_free = free_bytes;
    return stack_min_free;
}
volatile unsigned int clunet_queue_drops = 0;

ISR(USART_RXC_vect){
	unsigned char status = UCSRA;
	char byte = UDR;
	uart_rx_last_at = systime;
	if (status & ((1<<FE)|(1<<DOR)|(1<<PE))) { uart_hardware_errors++; uart_rx_overflow = 1; return; }
	if (uart_rx_data_len < UART_RX_BUF_LENGTH){
		uart_rx_data[uart_rx_data_len++] = byte;
	}else{
		uart_rx_overflow = 1;
        uart_overflows++;
	}
}
	
#define UART_TX_BUF_LENGTH 96	//Keep TX framing symmetric with RX and leave some guard space
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
	if (uart_ready_to_send() && length <= UART_TX_BUF_LENGTH - 6){
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
		if (!clunet_buffered_push(src_address, dst_address, command, data, size)) clunet_queue_drops++;
	}
}

char button_value;

static void uart_rx_reset(){
	unsigned char sreg = SREG;
	cli();
	uart_rx_data_len = 0;
	uart_rx_overflow = 0;
	SREG = sreg;
}

void analyze_uart_rx_trim(unsigned char offset){
	unsigned char sreg = SREG;
	cli();
	if (offset <= uart_rx_data_len){
		uart_rx_data_len -= offset;
		if (uart_rx_data_len){
			memmove((void*)uart_rx_data, (void*)(uart_rx_data + offset), uart_rx_data_len);
		}
	}
	SREG = sreg;
}

void analyze_uart_rx(char(*f)(unsigned char code, char* data, unsigned char length)){
	if (uart_rx_overflow){
		uart_rx_reset();
		return;
	}

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
			if (length < 3 || length > (UART_RX_BUF_LENGTH - 2)){
				analyze_uart_rx_trim(2); 	//пришел мусор, отрезаем преамбулу и надо пробовать искать преамбулу снова
				continue;
			}
					
			if (uart_rx_data_len >= length+2){		//в буфере данных уже столько, сколько описано в поле length
				if (check_crc(uart_rx_message, length - 1) == uart_rx_message[length - 1]){ //проверка crc
					if (f){
						if (!f(uart_rx_message[1], &uart_rx_message[2], length - 3)) break;
					}					
					analyze_uart_rx_trim(length+2); //отрезаем прочитанное сообщение
				}else{
                    uart_crc_errors++;
					analyze_uart_rx_trim(2); 
				}
			}else{
                if ((unsigned char)(systime - uart_rx_last_at) >= 100) {
                    analyze_uart_rx_trim(2); continue;
                }
				break;
			}
		}else{
            if ((unsigned char)(systime - uart_rx_last_at) >= 100) {
                analyze_uart_rx_trim(2); continue;
            }
			break;
		}
	}
}

// UART application protocol v2. Bootloader code 1 remains unchanged.
static unsigned int bridge_now = 0, tx_id = 0, tx_started = 0, tx_budget = 0;
static unsigned char bridge_last_tick = 0, tx_status = 0, legacy_waiting = 0;
static unsigned int legacy_since = 0;

void service_transport(){
    unsigned char tick = systime;
    bridge_now += (unsigned char)(tick - bridge_last_tick);
    bridge_last_tick = tick;
    if (tx_status == 1) {
        if ((unsigned int)(bridge_now - tx_started) >= tx_budget) clunet_expire_tracked();
        if (clunetTrackedResult) tx_status = clunetTrackedResult;
    }
}

char on_uart_message(unsigned char code, char* data, unsigned char length){
    if (code != UART_MESSAGE_CODE_CLUNET) legacy_waiting = 0;
    if (code == 3 && length == 1){
        if (!uart_ready_to_send()) return 0;
        service_transport();
        char reply[18] = {data[0], 2, clunetSendingState == CLUNET_SENDING_STATE_IDLE && tx_status != 1,
                          (char)tx_id, (char)(tx_id >> 8), (char)tx_status};
        unsigned char saved = SREG; cli();
        reply[6] = uart_hardware_errors; reply[7] = uart_hardware_errors >> 8;
        reply[8] = clunet_queue_drops; reply[9] = clunet_queue_drops >> 8;
        reply[10] = uart_overflows; reply[11] = uart_overflows >> 8;
        reply[12] = legacy_expired; reply[13] = legacy_expired >> 8;
        reply[14] = uart_crc_errors; reply[15] = uart_crc_errors >> 8;
        SREG = saved;
        unsigned int stack_free = stack_watermark();
        reply[16] = stack_free; reply[17] = stack_free >> 8;
        return uart_send_message(4, reply, sizeof(reply)) != 0;
    }
    if (code == 7 && length == 2) {
        unsigned int id = (unsigned char)data[0] | ((unsigned int)(unsigned char)data[1] << 8);
        if (id == tx_id && tx_status == 1) clunet_expire_tracked();
        return 1;
    }
    if (code == 5 && length >= 8) {
        unsigned int id = (unsigned char)data[0] | ((unsigned int)(unsigned char)data[1] << 8);
        unsigned int budget = (unsigned char)data[2] | ((unsigned int)(unsigned char)data[3] << 8);
        unsigned char src = data[4], dst = data[5], command = data[6], size = data[7];
        if (!id || id == tx_id || tx_status == 1) return 1; // Duplicate submissions never re-execute.
        tx_id = id; tx_status = 4; // Explicit rejection until validation and admission succeed.
        if (!budget || budget > 2000 || size > 68 || length != 8 + size || !CLUNET_MULTICAST_DEVICE(src)) return 1;
        if (!clunet_try_send_tracked(src, dst, CLUNET_PRIORITY_MESSAGE, command, data + 8, size)) return 1;
        tx_started = bridge_now; tx_budget = budget; tx_status = 1;
        discovery_listen_header(dst, command);
        return 1;
    }
    if (code == UART_MESSAGE_CODE_CLUNET && data && length >= 4){
        unsigned char src = data[0], dst = data[1], command = data[2], size = data[3];
        if (size > 68 || length != 4 + size) { legacy_waiting = 0; return 1; }
        if (!legacy_waiting) { legacy_waiting = 1; legacy_since = bridge_now; }
        if ((unsigned int)(bridge_now - legacy_since) >= 2000) { legacy_waiting = 0; legacy_expired++; return 1; }
        if (CLUNET_MULTICAST_DEVICE(src)){
            if (tx_status == 1 || !clunet_try_send_tracked(src, dst, CLUNET_PRIORITY_MESSAGE, command, data + 4, size)) return 0;
            tx_id = 0; tx_started = bridge_now; tx_budget = 2000; tx_status = 1;
            legacy_waiting = 0;
            discovery_listen_header(dst, command);
        }
    }
    return 1;
}

// Alternate credit replies and queued events when both directions stay busy.
void service_uart(){
    static unsigned char prefer_events = 0;
    if (!uart_ready_to_send()) { analyze_uart_rx(on_uart_message); return; }
    if (!prefer_events) {
        analyze_uart_rx(on_uart_message);
        if (!uart_ready_to_send()) { prefer_events = 1; return; }
    }
    clunet_msg* msg = clunet_buffered_peek();
    if (msg && uart_send_message(UART_MESSAGE_CODE_CLUNET, (char*)msg, 4 + msg->size)) {
        discovery_listen(msg);
        clunet_buffered_pop();
        prefer_events = 0;
    }
    analyze_uart_rx(on_uart_message);
}

int main(void){
	cli();
    init_stack_watermark();
	
	wdt_enable(WDTO_2S);
	
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
        service_transport();
		service_uart();
			
		if (prev_systime != systime){
			
			unsigned int delta_ms_time = TIMER_PERIOD*(unsigned char)(systime - prev_systime);
			prev_systime = systime;
			
			if (discovery_observe_time){
				discovery_observe_time = delta_ms_time >= (unsigned int)discovery_observe_time ? 0 : discovery_observe_time - delta_ms_time;
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
				button_value = new_button_value;
				if (!new_button_value){
					discovery_broadcast();
				}
			}
		}
		wdt_reset();
	}
	
	return 0;
}
