
//protocol delays (mks)
#define RF_START_BIT1_DELAY 3000
#define RF_START_BIT0_DELAY 100

#define RF_BIT0_DELAY 100
#define RF_BIT1_DELAY 150
#define RF_HI_DELAY 75

//PROTOCOL DEFINITION

// | preambule | device id | message id |    data   |  CRC   |
// |  6 bits	  2 bits   |   1 byte	|  n bytes  | 1 byte |

//static preambule (0b10101100)
#define RF_MESSAGE_PREAMBULE_HEADER 172


//device descriptors:
//2 bits device id
#define RF_RGB_LIGHTS_ID 2
//data length
#define RF_RGB_LIGHTS_DATA_LEN 10