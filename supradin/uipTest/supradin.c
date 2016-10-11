/*
 * uipTest.c
 *
 * Created: 08.12.2014 22:31:51
 *  Author: gargon
 */ 


#include "enc28j60/enc28j60.h"
#include "uIP/uip.h"
#include "uIP/uip_arp.h"
#include "clunet/clunet.h"
#include "supradin.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#define BUF ((struct uip_eth_hdr *)&uip_buf[0])

volatile uint16_t poll_counter_c = 0;

//periodic poll of connections
ISR(TIMER0_OVF_vect){
	++poll_counter_c;
}

void ethernet_send(){
	enc28j60_send_packet((uint8_t *) uip_buf, uip_len);
}

void check_ethernet(){
	uip_len = enc28j60_recv_packet((uint8_t *) uip_buf, UIP_BUFSIZE);

	if (uip_len > 0){
		if (BUF->type == htons(UIP_ETHTYPE_IP)){
			uip_arp_ipin();
			uip_input();
			
			if (uip_len > 0){
				uip_arp_out();      
				ethernet_send();
			}
			
		} else if (BUF->type == htons(UIP_ETHTYPE_ARP)){
			uip_arp_arpin();
			if (uip_len > 0){
				ethernet_send();
			}
		}
	}
}

void periodic_ethernet(){
	if (poll_counter_c >= ETH_ARP_TIMER_COUNTER){ //each 10 sec
		
		//decrease timer
		for(uint8_t i = 0; i < UIP_UDP_CONNS; i++) {
			struct uip_udp_conn *c = &uip_udp_conns[i];
			uip_udp_appstate_t *s = &c->appstate;
			--s->timer;
		}
		
		//arp timer
		poll_counter_c = 0;
		uip_arp_timer();
	}
}

void poll_ethernet(){
	for(uint8_t i = 0; i < UIP_UDP_CONNS; i++) {
		uip_udp_periodic(i);
		if(uip_len > 0) {
			uip_arp_out();
			ethernet_send();
		}
	}
}


int main(void){
	wdt_disable(); 
	
	//define mac (never change the first byte 0x00)
	struct uip_eth_addr mac = {{ 0xFA, 0xD8, 0x49, 0x34, 0x68, 0x23 }};	
	enc28j60_init(mac.addr);

	//stack init
	uip_init();
	uip_arp_init();

	//application init
	uip_app_init();

	//set mac
	uip_setethaddr(mac);

	//set ip, etc
	uip_ipaddr_t ipaddr;
	uip_ipaddr(ipaddr, 192, 168, 1, 10);
	uip_sethostaddr(ipaddr);
	uip_ipaddr(ipaddr, 192, 168, 1, 1);
	uip_setdraddr(ipaddr);
	uip_ipaddr(ipaddr, 255, 255, 255, 0);
	uip_setnetmask(ipaddr);	
	
	//enable ethernet poll timer
	ETH_POLL_TIMER_INIT;
	ENABLE_ETH_POLL_TIMER_OVF;

	clunet_init();
	clunet_set_on_data_received_sniff(clunet_data_received);


    wdt_enable(WDTO_2S);
	 
    while(1){
		check_ethernet();
		poll_ethernet();
		//clunet_data_flush();
		
		periodic_ethernet();
		 
		wdt_reset();
    }
}