#!/usr/bin/env python3
"""Actual AVR dispatch/driver and ESP flash gating with deterministic hardware stubs."""
import argparse,subprocess,tempfile,re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);root=Path(__file__).resolve().parents[4];sketch=Path(__file__).resolve().parents[1]
p.add_argument('--driver',type=Path,default=root/'_lib/clunet/clunet.c');p.add_argument('--bridge',type=Path,default=root/'bridge/src/Bridge/Bridge.c');a=p.parse_args()
def fun(s,key):
 start=s.index(key);b=s.index('{',start);end=b+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
driver=a.driver.read_text();driver=re.sub(r'#if CLUNET_AUTOREPLY_IDLE_ONLY\n(.*?)#else\n.*?#endif',r'\1',driver,flags=re.S);bridge=a.bridge.read_text();flash=(sketch/'FlashFirmware.cpp').read_text();w=Path(tempfile.mkdtemp(prefix='concurrency-regression-'))
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "BootloaderLease.h"
#include "LegacyFlashFlow.h"
namespace driver_test {
#define CLUNET_SENDING_STATE_IDLE 0
#define CLUNET_SENDING_STATE_INIT 1
#define CLUNET_SENDING_STATE_PREPARING 5
#define CLUNET_SENDING_STATE_WAITING_LINE 6
#define CLUNET_SENDING_STATE_PREINIT 7
#define CLUNET_READING_STATE_IDLE 0
#define CLUNET_PRIORITY_MESSAGE 3
#define CLUNET_PRIORITY_COMMAND 4
#define CLUNET_DEVICE_ID 0
#define CLUNET_BROADCAST_ADDRESS 255
#define CLUNET_COMMAND_DISCOVERY 0
#define CLUNET_COMMAND_DISCOVERY_RESPONSE 1
#define CLUNET_COMMAND_REBOOT 3
#define CLUNET_COMMAND_PING 254
#define CLUNET_COMMAND_PING_REPLY 255
#define CLUNET_OFFSET_SRC_ADDRESS 0
#define CLUNET_OFFSET_DST_ADDRESS 1
#define CLUNET_OFFSET_COMMAND 2
#define CLUNET_OFFSET_SIZE 3
#define CLUNET_OFFSET_DATA 4
#define CLUNET_SEND_BUFFER_SIZE 133
#define CLUNET_AUTOREPLY_PING_DISCOVERY 1
#define CLUNET_AUTOREPLY_IDLE_ONLY 1
#define CLUNET_READING 1
#define WDE 0
#define set_bit(a,b) ((a)|=1<<(b))
unsigned char SREG=128,WDTCR=0,clunetSendingState=0,clunetReadingState=0,clunetCurrentPrio=0;
unsigned char clunetTrackedResult=0,clunetTrackedActive=0,clunetExpirePending=0;
char dataToSend[133]; unsigned clunetSendingDataLength;
void (*on_data_received)(unsigned char,unsigned char,unsigned char,char*,unsigned char)=nullptr;
void (*on_data_received_sniff)(unsigned char,unsigned char,unsigned char,char*,unsigned char)=nullptr;
int autoReplies=0,starts=0,injected=0;
void cli(){SREG=0;}
void clunet_start_send(){assert(!SREG);clunetSendingState=1;++starts;}
void clunet_send(unsigned char,unsigned char,unsigned char,char*,unsigned char){++autoReplies;}
void clunet_data_received(unsigned char,unsigned char,unsigned char,char*,unsigned char);
char check_crc(char*,unsigned char){
 assert(SREG==128 && clunetSendingState==CLUNET_SENDING_STATE_PREPARING);
 int before=autoReplies;
 // Receive ISR arrives during CRC preparation: it must not replace the reserved buffer.
 clunet_data_received(20,255,0,nullptr,0);assert(autoReplies==before);++injected;
 return 42;
}
'''+fun(driver,'static void clunet_prepare_packet(')+'\n'+fun(driver,'static unsigned char clunet_try_send_mode(')+'\n'+fun(driver,'unsigned char clunet_try_send_fake(')+'\n'+fun(driver,'inline void clunet_data_received(')+r'''
void run(){
 for(int state=1;state<=9;state++)for(int priority=1;priority<=4;priority++){
  clunetSendingState=state;clunetCurrentPrio=priority;int before=autoReplies;
  clunet_data_received(20,255,0,nullptr,0);clunet_data_received(20,0,254,nullptr,0);assert(autoReplies==before);
 }
 clunetSendingState=0;clunet_data_received(20,255,0,nullptr,0);assert(autoReplies==1);
 clunetSendingState=0;clunet_data_received(20,0,254,nullptr,0);assert(autoReplies==2);
 clunetSendingState=1;memset(dataToSend,99,sizeof(dataToSend));SREG=128;
 assert(!clunet_try_send_fake(128,20,3,32,nullptr,0));assert(dataToSend[0]==99 && SREG==128);
 clunetSendingState=0;char value=7;assert(clunet_try_send_fake(128,20,3,32,&value,1));
 assert(starts==1 && SREG==128 && injected==1 && dataToSend[0]==char(128) && dataToSend[4]==7 && dataToSend[5]==42);
 clunetSendingState=0;clunetReadingState=1;assert(clunet_try_send_fake(128,20,3,32,nullptr,0));assert(starts==1 && clunetSendingState==6);
}
}
namespace service_test {
struct clunet_msg {char bytes[4];unsigned char size=0;};
clunet_msg packet; int events=0,credits=0,pendingCredits=0,pendingEvents=0;bool txBusy=false;
char uart_ready_to_send(){return !txBusy;}
char on_uart_message(unsigned char,char*,unsigned char){return 1;}
void analyze_uart_rx(char(*)(unsigned char,char*,unsigned char)){
 if(pendingCredits && !txBusy){++credits;--pendingCredits;txBusy=true;}
}
clunet_msg* clunet_buffered_peek(){return pendingEvents?&packet:nullptr;}
char uart_send_message(char,char*,unsigned char){assert(!txBusy);txBusy=true;++events;return 1;}
void clunet_buffered_pop(){--pendingEvents;}
void discovery_listen(clunet_msg*){}
#define UART_MESSAGE_CODE_CLUNET 1
'''+fun(bridge,'void service_uart(')+r'''
void run(){
 pendingEvents=pendingCredits=1000;
 for(int i=0;i<2000;i++){txBusy=false;service_uart(); assert(events-credits<=1 && credits-events<=1); for(int j=0;j<3;j++)service_uart();}
 assert(events==1000 && credits==1000);
 pendingEvents=200;for(int i=0;i<200;i++){txBusy=false;service_uart();}assert(pendingEvents==0);
 pendingCredits=200;for(int i=0;i<200;i++){txBusy=false;service_uart();}assert(pendingCredits==0);
}
}
namespace flash_test {
#define CLUNET_COMMAND_BOOT_CONTROL 2
#define COMMAND_FIRMWARE_UPDATE_START 0
struct IPAddress {uint32_t value;explicit IPAddress(uint32_t v=0):value(v){} operator uint32_t()const{return value;}};
struct clunet_packet {uint8_t src,dst,command,size;char data[1];};
BootloaderLease bootloaderLease; LegacyFlashFlow externalFlow; bool legacyApplicationSeen=false;
namespace BridgeTransport {void legacyBootStarted(){}}
struct {bool active=false;uint8_t target=20;} flashSession;
uint32_t now=0;uint32_t millis(){return now;}
bool isBootloaderUartIsolated(uint8_t address=0);
'''+ '\n'.join(fun(flash,f) for f in ['bool isBootloaderUartIsolated(', 'void observeBootloaderResponse(', 'bool forwardingStartsBootloaderSession(', 'bool shouldForwardMulticastToUart(', 'void recordForwardedPacket('])+r'''
void run(){
 clunet_packet a={238,20,2,1,{1}},b=a;IPAddress ip(1),other(2);
 assert(shouldForwardMulticastToUart(&a,ip,123));recordForwardedPacket(&a,ip,123);
 assert(!shouldForwardMulticastToUart(&a,other,123));assert(!shouldForwardMulticastToUart(&a,ip,124));
 b.src=239;assert(!shouldForwardMulticastToUart(&b,ip,123));b=a;b.dst=21;assert(!shouldForwardMulticastToUart(&b,ip,123));
 assert(!shouldForwardMulticastToUart(&a,ip,123)); // Duplicate INIT is unsafe.
 now=9999;assert(!shouldForwardMulticastToUart(&b,other,123));now=10000;assert(shouldForwardMulticastToUart(&b,other,123));
 // Rejections do not prolong the lease or change target. START from other devices does not steal it.
 recordForwardedPacket(&a,ip,123);clunet_packet response={21,255,2,1,{0}};observeBootloaderResponse(&response);
 assert(isBootloaderUartIsolated(20) && !isBootloaderUartIsolated(21));
 flashSession.active=true;assert(!shouldForwardMulticastToUart(&a,ip,123));flashSession.active=false;
 now=20000;assert(!shouldForwardMulticastToUart(&a,IPAddress(),0));
 bootloaderLease=BootloaderLease();response.src=20;observeBootloaderResponse(&response);assert(!bootloaderLease.owned(now));
 assert(!shouldForwardMulticastToUart(&b,other,123));assert(shouldForwardMulticastToUart(&a,ip,123));
 recordForwardedPacket(&a,ip,123);assert(!forwardingStartsBootloaderSession(&a));
 bootloaderLease=BootloaderLease();now=UINT32_MAX-5;recordForwardedPacket(&a,ip,123);now=10;assert(!shouldForwardMulticastToUart(&b,other,123));
}
}
int main(){driver_test::run();service_test::run();flash_test::run();puts("PASS: busy autoreplies, ISR during TX preparation, atomic admission, UART fairness, flash owner/IP/port/target, rejected traffic and timeout wrap, HTTP upload ownership");}
'''
upload = flash[flash.index('  server.on("/flash/firmware"'):flash.index('\n}\n\nvoid process()', flash.index('  server.on("/flash/firmware"'))]
extra = r'''
#include <string>
#include <functional>
namespace upload_test {
using String=std::string;
struct AsyncResponseStream{};
struct AsyncWebServerRequest {
 int status=0;std::function<void()> disconnected;
 void onDisconnect(std::function<void()> f){disconnected=f;}
 void send(int code,const char*,String){status=code;}
 void send(AsyncResponseStream*){status=200;}
};
struct {
 std::function<void(AsyncWebServerRequest*)> finish;
 std::function<void(AsyncWebServerRequest*,String,size_t,uint8_t*,size_t,bool)> chunk;
 template<class F,class G> void on(const char*,int,F f,G g){finish=f;chunk=g;}
} server;
const int HTTP_POST=1;
const uint32_t FLASH_UPLOAD_MAX_RAW_SIZE=65535;
struct {bool active=false;} flashSession;
struct {bool hasError=false,inProgress=false;uint32_t rawSize=0,baseOffset=0,lineLength=0;char error[96]={};} flashUpload;
AsyncWebServerRequest* flashUploadRequest=nullptr;
bool flashFirmwareReady=false;int resets=0,bytes=0;
void flashResetUploadState(){++resets;bytes=0;flashFirmwareReady=false;flashUpload={};}
void flashSetUploadError(const char* s){flashUpload.hasError=true;strcpy(flashUpload.error,s);}
void flashUploadConsumeChunk(uint8_t*,size_t n){bytes+=n;}
void flashUploadFinalize(){flashUpload.inProgress=false;flashFirmwareReady=true;}
AsyncResponseStream* beginFlashResponse(AsyncWebServerRequest*){return nullptr;}
void fillFlashStatusResponse(AsyncResponseStream*){}
void setup(){
''' + upload + r'''
}
void run(){
 setup();AsyncWebServerRequest a,b,c;uint8_t data=1;
 server.chunk(&a,"f",0,&data,1,false);assert(resets==1 && bytes==1);
 server.chunk(&b,"g",0,&data,1,false);server.chunk(&b,"g",1,&data,1,true);server.finish(&b);
 assert(b.status==409 && resets==1 && bytes==1 && flashUploadRequest==&a);
 server.chunk(&a,"f",1,&data,1,true);server.finish(&a);assert(a.status==200 && flashFirmwareReady && bytes==2);
 a.disconnected();assert(flashFirmwareReady);
 flashSession.active=true;server.chunk(&c,"f",0,&data,1,true);server.finish(&c);
 assert(c.status==409 && resets==1 && bytes==2 && flashFirmwareReady);
 flashSession.active=false;server.chunk(&a,"f",0,&data,1,false);a.disconnected();
 assert(!flashUploadRequest && !flashUpload.inProgress && !flashFirmwareReady);
 server.chunk(&c,"f",0,&data,1,true);server.finish(&c);assert(c.status==200 && bytes==1);
}
}
'''
code = code.replace('int main(){', extra+'\nint main(){upload_test::run();')

(w/'test.cpp').write_text(code)
subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(sketch),str(w/'test.cpp'),'-o',str(w/'test')],check=True)
subprocess.run([str(w/'test')],check=True)
