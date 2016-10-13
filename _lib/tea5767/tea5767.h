#ifndef TEA5767_H
#define TEA5767_H

#include <stdint.h>
#include <avr/eeprom.h>

#define TEA5767_SLA_W (0b11000000)		///< I2C write address
#define TEA5767_SLA_R (TEA5767_SLA_W | 0x01)	///< I2C read address


////////////////////////////////////////////////////////////////////////
// WRITE REGISTERS
////////////////////////////////////////////////////////////////////////

// REGISTER W1
#define TEA5767_MUTE		0x80	///< if MUTE = 1 then L and R audio are muted
#define TEA5767_SEARCH		0x40	///< Search mode: if SM = 1 then in search mode
//	Bits 0-5 for divider MSB (PLL 13:8)

// REGISTER W2
// 	Bits 0-7 for divider LSB (PLL 7:0)

// REGISTER W3
#define TEA5767_SUD	0x80			///< Search Up/Down: if SUD = 1 then search up; if SUD = 0 then search down
/// Search stop levels:
#define TEA5767_SRCH_HIGH_LVL	0x60///< ADC output = 10
#define TEA5767_SRCH_MID_LVL	0x40///< ADC output = 7
#define TEA5767_SRCH_LOW_LVL	0x20///< ADC output = 5

#define TEA5767_HIGH_LO_INJECT	0x10///< High/Low Side Injection
#define TEA5767_MONO		0x08	///< Force mono
#define TEA5767_MUTE_RIGHT	0x04	///< Mute right channel and force mono
#define TEA5767_MUTE_LEFT	0x02	///< Mute left channel and force mono
#define TEA5767_PORT1_HIGH	0x01	///< Software programmable port 1: if SWP1 = 1 then port 1 is HIGH; if SWP1 = 0 then port 1 is LOW

// REGISTER W4
#define TEA5767_PORT2_HIGH	0x80	///< Software programmable port 2: if SWP2 = 1 then port 2 is HIGH; if SWP2 = 0 then port 2 is LOW
#define TEA5767_STDBY		0x40	///< Standby: if STBY = 1 then in Standby mode (I2C remains active)
#define TEA5767_BAND_LIMIT	0x20	///< Band Limits: BL = 1 => Japan 76-108 MHz; BL = 0 => US/EU 87.5-108
#define TEA5767_XTAL		0x10	///< Set to 1 for 32.768 kHz XTAL
#define TEA5767_SOFT_MUTE	0x08	///< Mutes low signal
#define TEA5767_HCC			0x04	///< High Cut Control, gives the possibility to cut high frequencies
///< from the audio signal when a weak signal is received
#define TEA5767_SNC			0x02	///< Stereo Noise Cancelling
#define TEA5767_SRCH_IND	0x01	///< Search Indicator: if SI = 1 then pin SWPORT1 is output for the ready
///< flag; if SI = 0 then pin SWPORT1 is software programmable port 1
// REGISTER W5
#define TEA5767_PLLREF		0x80	///< Set to 0 for 32.768 kHz XTAL
#define TEA5767_DTC			0X40	///< if DTC = 1 then the de-emphasis time constant is 75 µs; if DTC = 0
///< then the de-emphasis time constant is 50 µs
///< Europe: used 50 us


////////////////////////////////////////////////////////////////////////
// READ REGISTERS
////////////////////////////////////////////////////////////////////////

// REGISTER R1
#define TEA5767_READY_FLAG	0x80	///< Ready Flag: RF = 1 Tuning completed or BL reached; RF = 0 busy
#define TEA5767_BAND_LIMIT_FLAG	0X40///< Band Limit Flag: if BLF = 1 then the band limit has been reached; if
///< BLF = 0 then the band limit has not been reached
//  bits 5...0: PLL[13:8] setting of synthesizer programmable counter after search or preset
#define TEA5767_PLL_MASK 0x3F

// REGISTER R2
//  bits 7...0: PLL[7:0] setting of synthesizer programmable counter after search or preset

// REGISTER R3
#define TEA5767_STEREO		0x80	///< stereo indicator
#define TEA5767_PLL			0x7f	///< IF counter result

// REGISTER R4
#define TEA5767_ADC_LEVEL	0xf0	///< level ADC output
#define TEA5767_CHIP_ID		0x0f	///< Chip Identification: these bits have to be set to logic 0

// REGISTER R5
/// - reserved for future use -


//setup the I2C hardware to ACK the next transmission
//and indicate that we've handled the last one.
#define TWACK (TWCR=(1<<TWINT)|(1<<TWEN)|(1<<TWEA))
//setup the I2C hardware to NACK the next transmission
#define TWNACK (TWCR=(1<<TWINT)|(1<<TWEN))


#define TEA5767N_MIN_FREQUENCY 8700
#define TEA5767N_MAX_FREQUENCY 10800


#define FM_MAX_NUM_CHANNELS	30
#define FM_PROGRAMS_EEPROM_OFFSET 0x40

uint8_t FM_clear_channels();
uint8_t FM_get_num_channels();
int8_t FM_save_channel(uint8_t num_channel, uint16_t frequency);
int8_t FM_add_channel(uint16_t frequency);
uint16_t FM_get_channel_frequency(uint8_t num_channel);

uint8_t FM_select_channel(uint8_t num_channel);
uint8_t FM_select_next_channel(uint8_t up);

uint8_t FM_select_frequency(uint16_t frequency);



#define FM_CONTROL_STANDBY 0
#define FM_CONTROL_MUTE 1
#define FM_CONTROL_MONO 2
#define FM_CONTROL_HCC 3
#define FM_CONTROL_SNC 4

uint8_t FM_control(uint8_t control, uint8_t on);


typedef struct{
	uint8_t type;			//Тип ответа (всегда 0)
	int8_t channel;			//Номер текущего канала
	uint16_t frequency;		//Частота
	uint8_t level;			//Уровень сигнала
	uint8_t stereo;			//Стерео
} fm_channel_info;

fm_channel_info* FM_channel_info();

typedef struct{
	uint8_t type;			//Тип ответа (всегда 1)
	uint8_t standby;		
	uint8_t mono;
	uint8_t mute;
	uint8_t hcc;
	uint8_t snc;
} fm_state_info;

fm_state_info* FM_state_info();

#endif