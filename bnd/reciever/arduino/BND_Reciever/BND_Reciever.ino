#include <SPI.h>
#include <RF24.h>
#include "nRF24L01.h"

//
// Hardware configuration
//

// Set up nRF24L01 radio on SPI bus plus pins 9 & 10

RF24 radio(9,10);

//
// Payload
//
int incomingByte = 0;   // for incoming serial data
struct payload_t
{
  uint8_t valid_th;
  uint8_t byte2;
  uint8_t byte3;
  uint8_t byte4;
  uint8_t byte5;
  uint8_t byte6;
  
  uint8_t valid_lightness;
  uint16_t lightness;
  uint8_t valid_voltage;
  uint16_t voltage;
};


void setup(void)
{
  //
  // Print preamble
  //

  Serial.begin(9600);
  Serial.println("Started");

  // Setup and configure rf radio according to the built-in parameters
  // of the FOB.
  //

  radio.begin();
  radio.setChannel(85);
  radio.setPayloadSize(12);
  radio.setAutoAck(true);
  radio.setCRCLength(RF24_CRC_16);
  radio.openReadingPipe(1,0xA1A1A1A1A1LL);
  radio.setDataRate( RF24_2MBPS ) ;

  //
  // Start listening
  //

  radio.startListening();

  //
  // Dump the configuration of the rf unit for debugging
  //

  //radio.printDetails();
}

//
// Loop
//

void loop(void)
{
  //
  // Receive each packet, dump it out
  //

    // if there is data ready
    if ( radio.available() )
   {
     // printf("got");
      // Get the packet from the radio
      payload_t payload;
      radio.read( &payload, sizeof(payload) );

      // Print the ID of this message.  Note that the message
      // is sent 'big-endian', so we have to flip it.
      float f = payload.byte4 & 0x7F;
      f *= 256;
      f += payload.byte5;
      f /= 10;
      if (payload.byte4 & 0x80)
	  f *= -1;

      Serial.print(millis());
      Serial.print("; ");

      Serial.print(f);
      Serial.print("*C; ");
      
      f = payload.byte2;
      f *= 256;
      f += payload.byte3;
      f /= 10;
      Serial.print(f);
      Serial.println("%");
	  
	  Serial.print(payload.valid_th, HEX);
	  Serial.print("\t\t");
	  
	  Serial.print(payload.byte2, HEX);
	  Serial.print("\t");
	  Serial.print(payload.byte3, HEX);
	  Serial.print("\t");
	  Serial.print(payload.byte4, HEX);
	  Serial.print("\t");
	  Serial.print(payload.byte5, HEX);
	  Serial.print("\t");
	  Serial.print(payload.byte6, HEX);
          
          Serial.print("\t\t");
          Serial.print(payload.valid_lightness);
          Serial.print("\t");
	  Serial.print(payload.lightness/100.0f);
          Serial.print("\t\t");
          Serial.print(payload.valid_voltage);
          Serial.print("\t");
	  Serial.println(payload.voltage/1000.0f);
    }
    
/*     // send data only when you receive data:
        if (Serial.available() > 0) {
                // read the incoming byte:
                incomingByte = Serial.read();

                // say what you got:
                Serial.print("I received: ");
                Serial.println(incomingByte, DEC);
        }
   */ 
}




