#ifndef __UIP_APP_H__
#define __UIP_APP_H__

/* Since this file will be included by uip.h, we cannot include uip.h
   here. But we might need to include uipopt.h if we need the u8_t and
   u16_t datatypes. */
#include "uIP/uipopt.h"

/**
 * The maxium age of udp DATA connection.
 *
 * An UDP_DATA_MAXAGE of 3 corresponds (in 10 seconds) to 30 seconds
 */
#define UDP_DATA_MAXAGE 3


/* Next, we define the uip_udp_appstate_t datatype. This is the state
   of our application, and the memory required for this state is
   allocated together with each UDP connection. One application state
   for each UDP connection. */
typedef struct uip_udp_app_state {
	enum {STATE_WAITING, STATE_SENT} state;
	char timer;
} uip_udp_appstate_t;


typedef struct uip_tcp_app_state {
} uip_tcp_appstate_t;


/* Finally we define the application function to be called by uIP. */
void uip_tcp_appcall(void);
#ifndef UIP_APPCALL
#define UIP_APPCALL uip_tcp_appcall
#endif /* UIP_APPCALL */

/* Finally we define the application function to be called by uIP. */
void uip_udp_appcall(void);
#ifndef UIP_UDP_APPCALL
#define UIP_UDP_APPCALL uip_udp_appcall
#endif /* UIP_UDP_APPCALL */

#define SUPRADIN_UDP_CONTROL_PORT 1234
#define SUPRADIN_UDP_DATA_PORT 1235

typedef struct supradin_control_request{
	uint16_t id;
	uint8_t type;
} supradin_control_request_t;

typedef struct supradin_control_response{
	supradin_control_request_t request;
	uint8_t result;
	uint8_t connection_active;
	
	uint8_t used_connections;	//for debugging only
	uint8_t free_connections;	//for debugging only
} supradin_control_response_t;

typedef struct supradin_header{
	union {
		uint8_t src_address;
		uint8_t prio;
		};
	uint8_t dst_address;
	uint8_t command;
	uint8_t size;	//data size
} supradin_header_t;

void uip_app_init(void);

void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size);

//

#endif /* __UIP_APP_H__ */
/** @} */
/** @} */
