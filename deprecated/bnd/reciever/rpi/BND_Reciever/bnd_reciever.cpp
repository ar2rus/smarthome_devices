#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <RF24/RF24.h>

using namespace std;

struct payload_t
{
  uint8_t valid_th;
  uint8_t th1;
  uint8_t th2;
  uint8_t th3;
  uint8_t th4;
  uint8_t th5;
  
  uint8_t valid_lightness;
  //uint16_t lightness;
  uint8_t lightness1;
  uint8_t lightness2;
  uint8_t valid_voltage;
  //uint16_t voltage;
  uint8_t voltage1;
  uint8_t voltage2;
};


//
// Hardware configuration
//

// CE Pin, CSN Pin, SPI Speed

// Setup for GPIO 22 CE and CE1 CSN with SPI Speed @ 1Mhz
//RF24 radio(RPI_V2_GPIO_P1_22, BCM2835_SPI_CS1, BCM2835_SPI_SPEED_1MHZ);

// Setup for GPIO 15 CE and CE0 CSN with SPI Speed @ 4Mhz
//RF24 radio(RPI_V2_GPIO_P1_15, BCM2835_SPI_CS0, BCM2835_SPI_SPEED_4MHZ);

// Setup for GPIO 15 CE and CE0 CSN with SPI Speed @ 8Mhz
RF24 radio(RPI_V2_GPIO_P1_15, RPI_V2_GPIO_P1_24, BCM2835_SPI_SPEED_8MHZ);


int main(int argc, char** argv){
  radio.begin();
  radio.setChannel(85);
  radio.setPayloadSize(12);
  radio.setAutoAck(true);
  radio.setCRCLength(RF24_CRC_16);
  radio.openReadingPipe(1,0xA1A1A1A1A1LL);
  radio.setDataRate( RF24_2MBPS ) ;

  radio.startListening();
  radio.printDetails();  // Dump the configuration of the rf unit for debugging
  // forever loop
  while (true){
	if ( radio.available() ){
      // Get the packet from the radio
      payload_t payload;
      radio.read(&payload, sizeof(payload));

      float t = payload.th3 & 0x7F;
      t *= 256;
      t += payload.th4;
      t /= 10;
      if (payload.th3 & 0x80)
	  t *= -1;

	  float h = payload.th1;
      h *= 256;
      h += payload.th2;
      h /= 10;
	  
	  double l = (payload.lightness2<<8)|payload.lightness1;
	  l /= 100.0d;
	  
	  double v = (payload.voltage2<<8)|payload.voltage1;
	  v /= 1000.0d;
	  
	  printf("%d\t%d\t%.1f*C\t%.1f%%\t%d\t%.2f%%\t%d\t%.3fV\n\r",millis(),payload.valid_th,t,h,payload.valid_lightness,l,payload.valid_voltage,v);
	 }
	 delay(500);
  } //while 1
} //main


