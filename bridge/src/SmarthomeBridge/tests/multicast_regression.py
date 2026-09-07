#!/usr/bin/env python3
"""Exercise the real multicast/request library with deterministic network and loop timers."""
import argparse
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--library',type=Path,default=Path.home()/'Documents/Arduino/libraries/ClunetMulticast/src')
a=p.parse_args(); w=Path(tempfile.mkdtemp(prefix='multicast-regression-'))
files={
'WString.h':r'''#pragma once
#include <string>
#include <functional>
#include <cstring>
#include <cstdlib>
using String = std::string;
''',
'ESP8266WiFi.h':r'''#pragma once
#include "WString.h"
struct IPAddress { unsigned value; IPAddress(unsigned a=0,unsigned b=0,unsigned c=0,unsigned d=0):value((a<<24)|(b<<16)|(c<<8)|d){} bool operator==(IPAddress b)const{return value==b.value;} };
struct WIFI { IPAddress localIP(){return IPAddress(192,168,1,120);} }; inline WIFI WiFi;
struct ESPType { void restart(){} }; inline ESPType ESP;
''',
'Ticker.h':r'''#pragma once
#include <functional>
struct Ticker {
 inline static std::function<void()> pending;
 inline static long delay=0;
 void detach(){pending=nullptr;}
 template<class F> void once_ms_scheduled(long ms,F f){delay=ms; pending=f;}
 static void fire(){auto f=pending; pending=nullptr; if(f)f();}
};
''',
'ESPAsyncUDP.h':r'''#pragma once
#include "ESP8266WiFi.h"
#include <vector>
struct AsyncUDPMessage {
 std::vector<uint8_t> bytes;
 AsyncUDPMessage(size_t){}
 void write(uint8_t c){bytes.push_back(c);}
 void write(uint8_t* p,size_t n){bytes.insert(bytes.end(),p,p+n);}
 size_t length(){return bytes.size();} uint8_t* data(){return bytes.data();}
};
struct AsyncUDPPacket {
 std::vector<uint8_t> bytes; IPAddress ip;
 bool isMulticast(){return true;} size_t length(){return bytes.size();}
 uint8_t* data(){return bytes.data();} IPAddress remoteIP(){return ip;} uint16_t remotePort(){return 12345;}
};
struct AsyncUDP {
 inline static std::function<void(AsyncUDPPacket)> receiver;
 inline static std::vector<uint8_t> sent;
 bool online=false;
 bool listenMulticast(IPAddress,int){online=true;return true;}
 template<class F> void onPacket(F f){receiver=f;}
 bool connected(){return online;} void close(){online=false;}
 size_t send(AsyncUDPMessage& m){sent=m.bytes;return sent.size();}
};
''',
'main.cpp':r'''#include "ClunetMulticast.h"
#include <cassert>
#include <cstdio>
int main(){
 ClunetMulticast c(128,"bridge"); int routed=0, sniffed=0, callbacks=0, replies=-1;
 c.onRouteSend([&](clunet_packet* p){++routed; assert(p->src==128); return p->len();});
 c.onPacketSniff([&](clunet_packet*){++sniffed;});
 c.onResponseReceived([&](int,LinkedList<clunet_response*>* q){++callbacks;replies=q->length();});
 // Direct route and ingest work even when multicast is unavailable.
 assert(c.send(20,0x60,nullptr,0)==4 && routed==1);
 auto filter=[](clunet_packet* p){return p->command==0x61 && p->size==1 && (uint8_t)p->data[0]==0xFE;};
 assert(c.request(20,0x60,nullptr,0,filter,1200)>0);
 assert(c.request(20,0x60,nullptr,0,filter,1200)==0);
 uint8_t bytes[]={21,128,0x61,1,0xFE}; auto packet=(clunet_packet*)bytes;
 c.ingest(packet,5); assert(Ticker::delay==1200); // Wrong source ignored.
 bytes[0]=20; bytes[4]=0xF0; c.ingest(packet,5); assert(Ticker::delay==1200);
 bytes[4]=0xFE; c.ingest(packet,4); assert(Ticker::delay==1200); // Truncated.
 c.ingest(packet,5); c.ingest(packet,5); assert(Ticker::delay==1);
 Ticker::fire(); assert(callbacks==1 && replies==1);
 assert(c.request(20,0x60,nullptr,0,filter,1200)>0);
 auto oldTimer=Ticker::pending; c.cancelRequest();
 assert(c.request(20,0x60,nullptr,0,filter,1200)>0);
 oldTimer(); assert(callbacks==1); Ticker::fire(); assert(callbacks==2 && replies==0);
 assert(c.request(255,0,nullptr,0,nullptr,100)>0);
 for(int i=0;i<40;i++){bytes[0]=i; c.ingest(packet,5);} Ticker::fire();
 assert(callbacks==3 && replies==16); // Bounded broadcast response history.
 assert(c.request(20,0,nullptr,0,nullptr,0)==0);
 int contextual=0; c.onPacketSniffFrom([&](clunet_packet*,IPAddress ip,uint16_t port){ assert(ip==IPAddress(192,168,1,2) && port==12345);++contextual; });
 c.ignoreOwnDatagrams(true); assert(c.connect()); int before=sniffed;
 AsyncUDP::receiver({{20,128,0x61,1,0xFE},WiFi.localIP()}); assert(sniffed==before);
 AsyncUDP::receiver({{20,128,0x61,1,0xFE},IPAddress(192,168,1,2)}); assert(sniffed==before+1 && contextual==1);
 AsyncUDP::receiver({{20,128,0x61,2,0xFE},IPAddress(192,168,1,2)}); assert(c.udpPacketsInvalidLength()==1);
 c.onRouteSend(nullptr); int sent=0;
 c.onPacketSent([&](clunet_packet* p){assert(p->src==128 && p->dst==20 && p->command==0x60 && p->size==1 && p->data[0]==42);++sent;});
 char payload=42; assert(c.send(20,0x60,&payload,1)==5 && sent==1);
 c.close(); assert(!c.send(20,0x60,&payload,1) && sent==1);
 puts("PASS: direct routing/ingest, source and subtype filters, early response, cancellation/stale timer, response cap, self-echo, sent callback");
}
'''}
for name,data in files.items():(w/name).write_text(data)
subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(w),'-I'+str(a.library),str(w/'main.cpp'),str(a.library/'ClunetMulticast.cpp'),'-o',str(w/'test')],check=True)
subprocess.run([str(w/'test')],check=True)
