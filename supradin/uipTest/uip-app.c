#include "uip-app.h"
#include "uIP/uip.h"

#include <avr/io.h>
#include <string.h>

#include "clunet/clunet.h"


struct uip_udp_conn* find_uip_udp_conn(uip_ipaddr_t *ripaddr, u16_t lport, u16_t rport ){
	for (uint8_t i = 0; i < UIP_UDP_CONNS; i++) {
		struct uip_udp_conn *c = &uip_udp_conns[i];
		if (c->lport == lport && c->rport == rport 
				&& uip_ipaddr_cmp(&c->ripaddr, ripaddr)){
					return c;
		}
	}
	return 0;
}

uint8_t supradin_frame_size;
char supradin_buffer[sizeof(supradin_header_t) + CLUNET_READ_BUFFER_SIZE];

void uip_app_init(void){
  struct uip_udp_conn *c = uip_udp_new(0, 0);
  if(c != 0) {
	uip_udp_bind(c, HTONS(SUPRADIN_UDP_CONTROL_PORT));
  }
}

void uip_tcp_appcall(void){
}

void uip_udp_appcall(void){
 if (uip_newdata()){
	 
	 switch (uip_udp_conn->lport){
		 case HTONS(SUPRADIN_UDP_CONTROL_PORT):
			if (uip_datalen() == sizeof(supradin_control_request_t)){
				
				struct uip_udpip_hdr *hdr = (struct uip_udpip_hdr *)&uip_buf[UIP_LLH_LEN];
				
				struct supradin_control_request *scrq  = uip_appdata;
				struct supradin_control_response *scrs = uip_appdata;
				
				scrs->result = 0;
				scrs->used_connections = 0;
				scrs->free_connections = 0;
				
				struct uip_udp_conn *conn = find_uip_udp_conn(&hdr->srcipaddr, HTONS(SUPRADIN_UDP_DATA_PORT), hdr->srcport);
				
				switch (scrq->type){
					case 0:		//ping and connect
						if (conn){
							//reset timer
							uip_udp_appstate_t *s = &conn->appstate;
							s->timer = UDP_DATA_MAXAGE;
							
							scrs->result = 1;
						}
						//break;				-> autoconnection for ping
					case 1:		//connect
						if (!conn){
							conn = uip_udp_new(0, hdr->srcport);
							if(conn) {
								uip_ipaddr_copy(&conn->ripaddr, hdr->srcipaddr);
								uip_udp_bind(conn, HTONS(SUPRADIN_UDP_DATA_PORT));
								
								uip_udp_appstate_t *s = &conn->appstate;
								s->state = STATE_SENT;
								s->timer = UDP_DATA_MAXAGE;
								
								scrs->result = 1;
							}
						}
						
						break;
					case 2:		//disconnect
						if (conn){
							uip_udp_remove(conn);
							conn = 0;
							scrs->result = 1;
						}
						break;
				}
				
				//status of current connection
				scrs->connection_active = (conn > 0);
				
				//calc used connections for info
				for (uint8_t i = 0; i < UIP_UDP_CONNS; i++) {
					if (uip_udp_conns[i].lport == HTONS(SUPRADIN_UDP_DATA_PORT)){	
						scrs->used_connections++;
					} else if (uip_udp_conns[i].lport == 0){
						scrs->free_connections++;
					}
				}
				
				//add all connections info	-> debugging
				/*memcpy(uip_appdata + sizeof(supradin_control_response_t), uip_udp_conns, sizeof(uip_udp_conns));*/
				
				//send reply with info (don't forget to reset ip and rport in poll)
				uip_ipaddr_copy(&uip_udp_conn->ripaddr, hdr->srcipaddr);
				uip_udp_conn->rport = hdr->srcport;
				uip_udp_send(sizeof(supradin_control_response_t) /*+ sizeof(uip_udp_conns)*/);	//debugging
				
			}
			break;
		 case HTONS(SUPRADIN_UDP_DATA_PORT):{
				struct supradin_header *ua = uip_appdata;
			
				//reset timer at first
				uip_udp_appstate_t *s = &uip_udp_conn->appstate;
				s->timer = UDP_DATA_MAXAGE;
			
				if (uip_datalen() == sizeof(supradin_header_t) + ua->size){
					
					//здесь реализованы 2 случая:
					//1) сообщения отправляемые клиентами модуля супрадин (ip = 0, в поле src_prio -> приоритет команды)
					//2) сообщения отправляемые мостом между сетями ethernet <-> supradin (ip реальный, в поле src_prio -> адрес отправителя)
					//Для использования моста необходимо передавать ненулевой IP адрес в пакете, а также следует не забывать указывать src_address устройства в поле prio,
					//при этом приоритет сообщения используется дефолтный
					
					unsigned char src_address;
					unsigned char prio;
					void* ip4;
					if (ua->ip4[0] == 0 && ua->ip4[1] == 0){	//supradin
						src_address = CLUNET_DEVICE_ID;	//отправляем от имени Supradin
						prio = ua->prio;				//используем переданный приоритет
						ip4 = &uip_udp_conn->ripaddr;	//добавляем IP-адрес клиента, подключенного к supradin
					}else{										//bridge
						src_address = ua->src_address;	//отправляем от имени отправителя
						prio = CLUNET_PRIORITY_MESSAGE; //используем приоритет по умолчанию
						ip4 = &ua->ip4;					//используем переданный IP-адрес устройства
					}
					
					
					//ждем завершения текущих отправок
					while(clunet_ready_to_send());
					//напрямую копируем ip в буфер для отправки клиентам
					supradin_header_t *sh = ((supradin_header_t *)&supradin_buffer);
					uip_ipaddr_copy(&sh->ip4, ip4);
					//вызываем скрытый метод отправки от любого имени
					clunet_send_fake(src_address, ua->dst_address, prio, ua->command, uip_appdata + sizeof(supradin_header_t), ua->size);
					
					//далее мы получим это сообщение в методе clunet_data_received
					//ip заранее подставлен в буфер для отправки
					
					//Внимание IP может быть только у src_id=0 или src_id>0x80
					
					
					
					//while(clunet_ready_to_send());
	
					//queue to send by udp with ip
					//clunet_data_received_(ip_, src_address, ua->dst_address, ua->command, uip_appdata + sizeof(supradin_header_t), ua->size);

				}
			}
			break;
	 }
 }else if (uip_poll()){
 	 if (uip_udp_conn->lport == HTONS(SUPRADIN_UDP_DATA_PORT)){
		  uip_udp_appstate_t *s = &uip_udp_conn->appstate;
			//check for timer
			if (s->timer <= 0){
				uip_udp_remove(uip_udp_conn);
			}else{
				if (s->state == STATE_WAITING){
					s->state = STATE_SENT;
					uip_send(supradin_buffer, supradin_frame_size);
				}	
			}
		}else if (uip_udp_conn->lport == HTONS(SUPRADIN_UDP_CONTROL_PORT)){
			//reset ip and rport after directed reply
			memset(uip_udp_conn->ripaddr, 0, sizeof(uip_ipaddr_t));
			uip_udp_conn->rport = 0;
		}else{	//unknown data -> clear it (see uip-conf.h, line 108)
			uip_udp_remove(uip_udp_conn);
		}
	}
}

//here we get clunet messages and queue them to udp sending
void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size){
	
	//check all connections are empty
	for (uint8_t i = 0; i < UIP_UDP_CONNS; i++) {
		struct uip_udp_conn *c = &uip_udp_conns[i];
		if (c->lport == HTONS(SUPRADIN_UDP_DATA_PORT)){
			uip_udp_appstate_t *s = &c->appstate;
			if (s->state == STATE_WAITING){
				return;	//если кому-то еще не отправили данные -> пропускаем пакет (хотя, по-хорошему, надо положить в очередь)
			}
		}
	}
	
	
	supradin_header_t *sh = ((supradin_header_t *)&supradin_buffer);
	
	sh->src_address = src_address;
	sh->dst_address = dst_address;
	sh->command = command;
	sh->size = size;
	
	memcpy(supradin_buffer + sizeof(supradin_header_t), data, size);
	supradin_frame_size = sizeof(supradin_header_t) + size;
	
	//set connections as 'waiting'
	for (uint8_t i = 0; i < UIP_UDP_CONNS; i++) {
		struct uip_udp_conn *c = &uip_udp_conns[i];
		if (c->lport == HTONS(SUPRADIN_UDP_DATA_PORT)){
			uip_udp_appstate_t *s = &c->appstate;
			s->state = STATE_WAITING;
		}
	}
}
 
/*---------------------------------------------------------------------------*/
