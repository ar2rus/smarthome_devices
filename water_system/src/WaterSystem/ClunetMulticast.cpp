
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "ClunetMulticast.h"
#include "ClunetMulticastConfig.h"

#define CLUNET_PORT 12345

// Multicast declarations
IPAddress clunetMulticastIP(234, 5, 6, 7);

// A UDP instance to let us send and receive packets over UDP
WiFiUDP clunetUDP;

char udp_buffer[CLUNET_BUFFER_SIZE];

void clunetMulticastBegin(){
  clunetUDP.beginMulticast(WiFi.localIP(), clunetMulticastIP, CLUNET_PORT);

  clunetMulticastSend(CLUNET_BROADCAST_ADDRESS, CLUNET_COMMAND_BOOT_COMPLETED, 0, 0);
}

char clunetMulticastHandleMessages(clunet_msg* msg){
  int packetSize = clunetUDP.parsePacket();
  if (packetSize) {
    int len = clunetUDP.read(udp_buffer, CLUNET_BUFFER_SIZE);
    if (len >= CLUNET_OFFSET_DATA) {
        clunet_msg* m = (clunet_msg*)&udp_buffer;
        
        switch(m->dst_address){
            case CLUNET_DEVICE_ID:
            case CLUNET_BROADCAST_ADDRESS:
              
              switch(m->command){
                case CLUNET_COMMAND_DISCOVERY:{
                    char buf[] = CLUNET_DEVICE_NAME;
                    int len = 0; while(buf[len]) len++;
                    clunetMulticastSend(m->src_address, CLUNET_COMMAND_DISCOVERY_RESPONSE, buf, len);
                  }
                  break;
                case CLUNET_COMMAND_PING:
                  clunetMulticastSend(m->src_address, CLUNET_COMMAND_PING_REPLY, m->data, m->size);
                  break;
                case CLUNET_COMMAND_REBOOT:
                  ESP.restart();
                  break;
                default:
                  //отдаем сообщение
                  memcpy(msg, udp_buffer, len);
                  return 1;
              }
            break;
        }
    }
  }
  return 0;
}

void clunetMulticastSend(unsigned char address, unsigned char command, char* data, unsigned char size){
  clunetUDP.beginPacketMulticast(clunetMulticastIP, CLUNET_PORT, WiFi.localIP());

  udp_buffer[CLUNET_OFFSET_SRC_ADDRESS] = CLUNET_DEVICE_ID;
  udp_buffer[CLUNET_OFFSET_DST_ADDRESS] = address;
  udp_buffer[CLUNET_OFFSET_COMMAND] = command;
  udp_buffer[CLUNET_OFFSET_SIZE] = size;
  memcpy(&udp_buffer[CLUNET_OFFSET_DATA], data, size);
    
  clunetUDP.write(udp_buffer, CLUNET_OFFSET_DATA + size);
  clunetUDP.endPacket();
}
