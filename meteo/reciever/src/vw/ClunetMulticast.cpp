
#include <ESP8266WiFi.h>
#include "ESPAsyncUDP.h"

#include "ClunetMulticast.h"
#include "ClunetMulticastConfig.h"

ClunetMulticast::ClunetMulticast(){
}

bool ClunetMulticast::connect(){
  //return _udp.connect(CLUNET_MULTICAST_IP, CLUNET_MULTICAST_PORT);
  return _udp.listenMulticast(CLUNET_MULTICAST_IP, CLUNET_MULTICAST_PORT);
}

void ClunetMulticast::close(){
  _udp.close();
}

void ClunetMulticast::onMessage(ClunetMulticastMessageHandlerFunction fn){
      _udp.onPacket([this,fn](AsyncUDPPacket packet) {
        if (packet.isMulticast()){
          if (packet.length() >= CLUNET_OFFSET_DATA) {
            
            clunet_message* m = (clunet_message*)packet.data();

            switch(m->dst_address){
                case CLUNET_DEVICE_ID:
                case CLUNET_BROADCAST_ADDRESS:
                  
                  switch(m->command){
                    case CLUNET_COMMAND_DISCOVERY:{
                        uint8_t buf[] = CLUNET_DEVICE_NAME;
                        uint8_t len = 0; while(buf[len]) len++;
                        send(m->src_address, CLUNET_COMMAND_DISCOVERY_RESPONSE, buf, len);
                      }
                      break;
                    case CLUNET_COMMAND_PING:
                      send(m->src_address, CLUNET_COMMAND_PING_REPLY, m->data, m->size);
                      break;
                    case CLUNET_COMMAND_REBOOT:
                      ESP.restart();
                      break;
                    default:
                      if (fn){
                        fn(m);
                      }
                  }
                break;
            }
          }
        }
  });
}

size_t ClunetMulticast::send(uint8_t address, uint8_t command, uint8_t* data, uint8_t size){
  size_t r = 0;
  if (_udp.connected()){
      AsyncUDPMessage* m = new AsyncUDPMessage(CLUNET_OFFSET_DATA + size);
      m->write(CLUNET_DEVICE_ID);
      m->write(address);
      m->write(command);
      m->write(size);
      m->write(data, size);
      r = _udp.send(*m);
      delete m;
  }
  return r;
}
