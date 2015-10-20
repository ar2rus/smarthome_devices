
#ifndef TEA5767_H_
#define TEA5767_H_

#include "tea5767_config.h"
#include "utils/bits.h"

#define TWI_TWBR 0x0C					// TWI Bit rate Register setting.	(02 - 400 KHz)


#define TEA5767_W_BYTE1_MUTE 7
#define TEA5767_W_BYTE1_SEARCH_MODE 6

#define TEA5767_W_BYTE3_SUD 7
#define TEA5767_W_BYTE3_SSL 5
#define TEA5767_W_BYTE3_HILO 4
#define TEA5767_W_BYTE3_MONO 3
#define TEA5767_W_BYTE3_MUTE_L 2
#define TEA5767_W_BYTE3_MUTE_R 1

#define TEA5767_W_BYTE4_STANDBY 6
#define TEA5767_W_BYTE4_BL 5
#define TEA5767_W_BYTE4_XTAL 4
#define TEA5767_W_BYTE4_SOFTMUTE 3
#define TEA5767_W_BYTE4_HCC 2
#define TEA5767_W_BYTE4_SNC 1

#define TEA5767_BL_JAPAN    1
#define TEA5767_BL_US_EURO  0

#define TEA5767_XTAL_32678KHZ    1
#define TEA5767_XTAL_13MHZ		 0

#define TEA5767_SUD_UP    1
#define TEA5767_SUD_DOWN  0

#define TEA5767_SSL_LOW  1
#define TEA5767_SSL_MID  2
#define TEA5767_SSL_HIGH 3


#define TEA5767_R_BYTE1_READY 7
#define TEA5767_R_BYTE1_BL	  6

#define TEA5767_R_BYTE3_STEREO  7
#define TEA5767_R_BYTE3_IF	    0

#define TEA5767_R_BYTE4_LEV	    4

#define TEA5767_DATA_SIZE 5				// TEA5767 data message size


void tea5767_init();
void tea5767_set_HI_PLL(double frequenc);
void tea5767_set_LO_PLL(double frequency);
char tea5767_search(char up, char ssl);


#endif /* TEA5767_H_ */