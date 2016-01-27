
#include "utils/bits.h"
#include "rf_config.h"

#include <avr/io.h>


//delays (mks)
#define RF_START_BIT_DELAY 250
#define RF_BIT0_DELAY 100
#define RF_BIT1_DELAY 150
#define RF_HI_DELAY 80

#ifdef RF_TIMER_PRESCALER
	//use at least 80% of RF_START_BIT_DELAY to detect start bit
	#define RF_NUM_TICKS_START (F_CPU * (RF_START_BIT_DELAY * 8 / 10)  / 1000000UL / RF_TIMER_PRESCALER)
	
	
	#if (RF_NUM_TICKS_START < 30)
		#error RF timer frequency is too small, increase CPU frequency or decrease timer prescaler
	#endif
	
	#if (RF_NUM_TICKS_START > 250)
		#error RF timer frequency is too big, decrease CPU frequency or increase timer prescaler
	#endif
	
	
	//use a half of difference between bit1 and bit0 delay to detect each of them
	#define RF_NUM_TICKS_BIT (F_CPU * (RF_BIT0_DELAY + (RF_BIT1_DELAY - RF_BIT0_DELAY) / 2) / 1000000UL / RF_TIMER_PRESCALER)
	
	#if (RF_NUM_TICKS_BIT < 10)
		#  error RF timer frequency is too small, increase CPU frequency or decrease timer prescaler
	#endif
	
	#if (RF_NUM_TICKS_BIT > 250)
		#  error RF timer frequency is too big, decrease CPU frequency or increase timer prescaler
	#endif
#endif


#define RF_TX_HI set_bit(OUTPORT(RF_PORT), RF_PIN)
#define RF_TX_LO unset_bit(OUTPORT(RF_PORT), RF_PIN)
#define RF_TX_INIT {set_bit(DDRPORT(RF_PORT), RF_PIN); RF_TX_LO;}


#ifdef RF_TIMER_INIT
	#define RF_RX_INIT {unset_bit(DDRPORT(RF_PORT), RF_PIN); RF_TIMER_INIT;}
	void rf_receive_packet(char* data, unsigned char num_to_recieve, char preambule);
	char rf_recieve_message(unsigned char device_id, char* data);
#endif

#define RF_VAL (test_bit(INPORT(RF_PORT), RF_PIN))

void rf_send_byte(char data);



//PROTOCOL DEFINITION

// | preambule | device id | message id |    data   |  CRC   |
// |  6 bits	  2 bits   |   1 byte	|  n bytes  | 1 byte |

//static preambule (0b10101100)
#define RF_MESSAGE_PREAMBULE_HEADER 172

void rf_send_message(unsigned char device_id, char* data, unsigned char num_repeats);


//device descriptors

//rgb lights:
//2 bits device id
#define RF_RGB_LIGHTS_ID 2
//data length
#define RF_RGB_LIGHTS_DATA_LEN 10