
#define USI_DELAY 20

// USI pin associated
#define sclport		PORTA
#define sclportd	DDRA
#define sdaport 	PORTA
#define sdaportd	DDRA
#define scl			4
#define sda			6
#define sclportinp	PINA
#define sdaportinp	PINA


/** defines the data direction (reading from I2C device) in i2c_start(),i2c_rep_start() */
#define I2C_READ    1

/** defines the data direction (writing to I2C device) in i2c_start(),i2c_rep_start() */
#define I2C_WRITE   0

extern void init_usi_i2c_master(void);
extern void i2c_start(void);
extern void i2c_stop(void);
//extern u08 usi_i2c_master_transfer(u08 tmp_sr);
extern uint8_t i2c_master_send(uint8_t *data, uint8_t data_size);
extern uint8_t i2c_master_read(uint8_t *data, uint8_t data_size);


//--------  end of file   -------------------------
