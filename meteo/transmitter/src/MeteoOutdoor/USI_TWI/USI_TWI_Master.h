/*****************************************************************************
*
* Atmel Corporation
*
* File              : USI_TWI_Master.h
* Compiler          : AVRGCC Toolchain version 3.4.2
* Revision          : $Revision: 992 $
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
*                     the USI_TWI_Start_Transceiver_With_Data() function. If the transceiver
*                     returns with a fail, then use USI_TWI_Get_Status_Info to evaluate the
*                     cause of the failure.
*
****************************************************************************/
    
//********** Defines **********//

// Defines controlling timing limits
#define TWI_HS_MODE

#ifdef TWI_HS_MODE
  #define T2_TWI    ((F_CPU *1.3) /1000000)
  #define T4_TWI    ((F_CPU * 0.6) /1000000)
#endif

#ifdef TWI_FAST_MODE               // TWI FAST mode timing limits. SCL = 100-400kHz
  #define T2_TWI    ((F_CPU *1300) /1000000) // >1,3us
  #define T4_TWI    ((F_CPU * 600) /1000000) // >0,6us
#endif

#ifdef TWI_STANDART_MODE          // TWI STANDARD mode timing limits. SCL <= 100kHz
  #define T2_TWI    ((F_CPU *4700) /1000000) // >4,7us
  #define T4_TWI    ((F_CPU *4000) /1000000) // >4,0us
#endif

/****************************************************************************
  Bit and byte definitions
****************************************************************************/
#define TWI_READ_BIT  0       // Bit position for R/W bit in "address byte".
#define TWI_ADR_BITS  1       // Bit position for LSB of the slave address bits in the init byte.
#define TWI_NACK_BIT  0       // Bit position for (N)ACK bit.

// Device dependent defines
#define DDR_USI             DDRA
#define PORT_USI            PORTA
#define PIN_USI             PINA
#define PORT_USI_SDA        PORTA6
#define PORT_USI_SCL        PORTA4
#define PIN_USI_SDA         PINA6
#define PIN_USI_SCL         PINA4

#define USISR_8BIT (1<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|(0x0<<USICNT0)      // Prepare register value to: Clear flags, and set USI to shift 8 bits i.e. count 16 clock edges.
#define USISR_1BIT (1<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|(0xE<<USICNT0)      // Prepare register value to: Clear flags, and set USI to shift 1 bit i.e. count 2 clock edges.

/** defines the data direction (reading from I2C device) in i2c_start(),i2c_rep_start() */
#define I2C_READ    1

/** defines the data direction (writing to I2C device) in i2c_start(),i2c_rep_start() */
#define I2C_WRITE   0


//********** Prototypes **********//

void USI_TWI_Master_Initialise();

void USI_TWI_Master_Start();
void USI_TWI_Master_Stop();

signed char USI_TWI_Master_SendData( char *msg, unsigned char msgSize);
signed char USI_TWI_Master_ReadData( char *msg, unsigned char msgSize);