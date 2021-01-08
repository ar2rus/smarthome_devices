/* Name: clunet_config.h
 * Project: CLUNET network driver
 * Author: Alexey Avdyukhin
 * Creation Date: 2012-11-08
 * License: DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 */
 
 #ifndef __clunet_config_h_included__
#define __clunet_config_h_included__

/* Device address (0-254) */
#define CLUNET_DEVICE_ID 0x1D
/*29 dec*/

/* Device name */
#define CLUNET_DEVICE_NAME "Kitchen"

/* Buffer sized (memory usage) */
/* 128 + header + crc; so we can send <=128 bytes */
#define CLUNET_SEND_BUFFER_SIZE 128 + CLUNET_OFFSET_DATA + 1
#define CLUNET_READ_BUFFER_SIZE 128 + CLUNET_OFFSET_DATA + 1

/* Pin to send data */
#define CLUNET_WRITE_PORT D
#define CLUNET_WRITE_PIN 2

/* Using transistor? */
#define CLUNET_WRITE_TRANSISTOR

/* Pin to receive data, external interrupt required! */
#define CLUNET_READ_PORT D
#define CLUNET_READ_PIN 3

/* Timer initialization */
#define CLUNET_TIMER_INIT {unset_bit4(TCCR2, WGM21, WGM20, COM21, COM20); /* Timer2, normal mode */ \
	set_bit2(TCCR2, CS21, CS20); unset_bit(TCCR2, CS22);  /* 32x prescaler for 8MHz*/ }
	
/* Timer prescaler */
#define CLUNET_TIMER_PRESCALER 32

/* Timer registers */
#define CLUNET_TIMER_REG TCNT2
#define CLUNET_TIMER_REG_OCR OCR2

/* How to enable and disable timer interrupts */
#define CLUNET_ENABLE_TIMER_COMP set_bit(TIMSK, OCIE2)
#define CLUNET_DISABLE_TIMER_COMP unset_bit(TIMSK, OCIE2)
#define CLUNET_ENABLE_TIMER_OVF set_bit(TIMSK, TOIE2)
#define CLUNET_DISABLE_TIMER_OVF unset_bit(TIMSK, TOIE2)

/* How to init and enable external interrupt (read pin) */
#define CLUNET_INIT_INT {set_bit(MCUCR,ISC10);unset_bit(MCUCR,ISC11); set_bit(GICR, INT1);}

/* Interrupt vectors */
#define CLUNET_TIMER_COMP_VECTOR TIMER2_COMP_vect
#define CLUNET_TIMER_OVF_VECTOR TIMER2_OVF_vect
#define CLUNET_INT_VECTOR INT1_vect


/************************************************************************/
/* НАСТРОЙКИ МОДУЛЯ clunet_buffered                                     */
/************************************************************************/

//Максимальный размер сообщения, который возможно сохранить в буффер.
//Устанавливать по максимальной длине сообщения (data) необходимых 
//к сохранению команд
#define CLUNET_BUFFERED_DATA_MAX_LENGTH 12

/* Размер буфера входящих сообщений 
	устанавливать, исходя из свободной ОЗУ (2-20 шт) 
*/
#define CLUNET_BUFFERED_BUFFER_SIZE 20

#endif
