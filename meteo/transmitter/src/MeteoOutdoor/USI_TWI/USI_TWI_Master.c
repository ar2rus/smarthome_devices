/*****************************************************************************
*
* Atmel Corporation
*
* File              : USI_TWI_Master.c
* Compiler          : IAR EWAAVR
* Revision          : $Revision: 6.21.1 $
* Date              : $Date: 2013-11-07 $
* Updated by        : $Author: Atmel $
*
* Support mail      : avr@atmel.com
*
* Supported devices : All device with USI module can be used.
*                     The example is written for the ATmega169, ATtiny26 and ATtiny2313
*
* AppNote           : AVR310 - Using the USI module as a TWI Master
*
* Description       : This is an implementation of an TWI master using
*                     the USI module as basis. The implementation assumes the AVR to
*                     be the only TWI master in the system and can therefore not be
*                     used in a multi-master system.
* Usage             : Initialize the USI module by calling the USI_TWI_Master_Initialise() 
*                     function. Hence messages/data are transceived on the bus using
*                     the USI_TWI_Transceive() function. The transceive function 
*                     returns a status byte, which can be used to evaluate the 
*                     success of the transmission.
*
****************************************************************************/
#include <avr/io.h>
#include "USI_TWI_Master.h"
#include <util/delay.h>

/*---------------------------------------------------------------
 USI TWI single master initialization function
---------------------------------------------------------------*/
void USI_TWI_Master_Initialise()
{
	
  PORT_USI |= (1<<PIN_USI_SDA);           // Enable pullup on SDA, to set high as released state.
  PORT_USI |= (1<<PIN_USI_SCL);           // Enable pullup on SCL, to set high as released state.
	
  DDR_USI  |= (1<<PIN_USI_SCL);           // Enable SCL as output.
  DDR_USI  |= (1<<PIN_USI_SDA);           // Enable SDA as output.
  
  USIDR    =  0xFF;                       // Preload dataregister with "released level" data.
  USICR    =  (0<<USISIE)|(0<<USIOIE)|                            // Disable Interrupts.
              (1<<USIWM1)|(0<<USIWM0)|                            // Set USI in Two-wire mode.
              (1<<USICS1)|(0<<USICS0)|(1<<USICLK)|                // Software stobe as counter clock source
              (0<<USITC);
  USISR   =   (1<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|      // Clear flags,
              (0x0<<USICNT0);                                     // and reset counter.
}

void USI_TWI_Master_Start(){
	
	/* Release SCL to ensure that (repeated) Start can be performed */
	//PORT_USI |= (1<<PIN_USI_SDA);                     // на всякий случай выставляем в исходное состояние sda
	PORT_USI |= (1<<PIN_USI_SCL);                     // Release SCL.
	//DDR_USI &= ~(1<<PIN_USI_SCL);					  //ВАЖНО!!! Отключаем SCL от выходного буфера интерфейса
	USISR = (1<<USISIF)
			|(1<<USIOIF)
			|(1<<USIPF)
			|(1<<USIDC)
			|(0x0<<USICNT0); 						 //сбрасываем USISR
	while( !(PIN_USI & (1<<PIN_USI_SCL)) );          // Verify that SCL becomes high.
 	
	//#ifdef TWI_FAST_MODE
 		_delay_us( T4_TWI/4 );                         //Delay for T4TWI if TWI_FAST_MODE
 	//#else
 	//	_delay_us( T2_TWI/4 );                         //Delay for T2TWI if TWI_STANDARD_MODE
 	//#endif

	/* Generate Start Condition */
	PORT_USI &= ~(1<<PIN_USI_SDA);                    // Force SDA LOW.
	_delay_us( T4_TWI/4 );
	//DDR_USI |= (1<<PIN_USI_SCL);					  //ВАЖНО!!! подключаем SCL обратно к выходному буферу интерфейса
	PORT_USI &= ~(1<<PIN_USI_SCL);                    // Pull SCL LOW.
	_delay_us( T4_TWI/4 );							  // Не обязательно
	PORT_USI |= (1<<PIN_USI_SDA);                     // Release SDA.
	_delay_us( T4_TWI/4 );							  //еще раз тупим задержку
}

void USI_TWI_Master_Stop() {
  PORT_USI &= ~(1<<PIN_USI_SDA);           // Pull SDA low.
  PORT_USI |= (1<<PIN_USI_SCL);            // Release SCL.
  while( !(PIN_USI & (1<<PIN_USI_SCL)) );  // Wait for SCL to go high.
  _delay_us( T4_TWI/4 );               
  PORT_USI |= (1<<PIN_USI_SDA);            // Release SDA.
  _delay_us( T2_TWI/4 );                

  USISR|=(1<<USIPF); 					  //сброс флага детекции состояния Стоп в USISR
}

unsigned char USI_TWI_Master_Transfer( unsigned char temp ){
  USISR = temp;                                     // Set USISR according to temp.
                                                    // Prepare clocking.
  temp  =  (0<<USISIE)|(0<<USIOIE)|                 // Interrupts disabled
           (1<<USIWM1)|(0<<USIWM0)|                 // Set USI in Two-wire mode.
           (1<<USICS1)|(0<<USICS0)|(1<<USICLK)|     // Software clock strobe as source.
           (1<<USITC);                              // Toggle Clock Port.
  do
  {
    _delay_us( T2_TWI/4 );              
    USICR = temp;                          // Generate positve SCL edge.
    while( !(PIN_USI & (1<<PIN_USI_SCL)) );// Wait for SCL to go high.
    _delay_us( T2_TWI/4 );              
    USICR = temp;                          // Generate negative SCL edge.
  }while( !(USISR & (1<<USIOIF)) );        // Check for transfer complete.
  
  _delay_us( T2_TWI/4 );                 
  temp  = USIDR;                           // Read out data.
  USIDR = 0xFF;                            // Release SDA.
  DDR_USI |= (1<<PIN_USI_SDA);             // Enable SDA as output.

  return temp;                             // Return the data from the USIDR
}

signed char USI_TWI_Master_SendData( char *msg, unsigned char msgSize){
	if (PIN_USI & (1<<PIN_USI_SCL)){
		return 0;
	}
	
	do {
		/* Write a byte */
		PORT_USI &= ~(1<<PIN_USI_SCL);                // Pull SCL LOW. ???
		USIDR     = *(msg++);                         // Setup data.
		USI_TWI_Master_Transfer( USISR_8BIT );		  // Send 8 bits on bus.
		
		/* Clock and verify (N)ACK from slave */
		DDR_USI  &= ~(1<<PIN_USI_SDA);                // Enable SDA as input.
		if( USI_TWI_Master_Transfer( USISR_1BIT ) & (1<<TWI_NACK_BIT)){
			return -1;
		}
	}while( --msgSize) ;                             // Until all data sent/received.
	return 1;
}

signed char USI_TWI_Master_ReadData( char *msg, unsigned char msgSize){
	if (PIN_USI & (1<<PIN_USI_SCL)){
		return 0;
	}
	
	do{
		/* Read a data byte */
		DDR_USI   &= ~(1<<PIN_USI_SDA);               // Enable SDA as input.
		*(msg++)  = USI_TWI_Master_Transfer( USISR_8BIT );

		/* Prepare to generate ACK (or NACK in case of End Of Transmission) */
		if( msgSize == 1){                            // If transmission of last byte was performed.
			USIDR = 0xFF;                              // Load NACK to confirm End Of Transmission.
		} else {
			USIDR = 0x00;                              // Load ACK. Set data register bit 7 (output for SDA) low.
		}
		USI_TWI_Master_Transfer( USISR_1BIT );       // Generate ACK/NACK.
	}while( --msgSize) ;                             // Until all data sent/received.
	return 1;
}
