#!/usr/bin/env python3
"""Exercise real UART v2 transport, AVR deadline/ISR, legacy flash guards and host verification."""
import argparse,re,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);root=Path(__file__).resolve().parents[4];sketch=Path(__file__).resolve().parents[1]
p.add_argument('--driver',type=Path,default=root/'_lib/clunet/clunet.c');p.add_argument('--bridge',type=Path,default=root/'bridge/src/Bridge/Bridge.c');a=p.parse_args()
def fun(s,key):
 start=s.index(key);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
w=Path(tempfile.mkdtemp(prefix='delivery-regression-'));driver=a.driver.read_text();avr=a.bridge.read_text();flash=(sketch/'FlashFirmware.cpp').read_text()
(w/'Arduino.h').write_text('#pragma once\n#include <cstdint>\n#include <cstddef>\n#include <cstring>\nuint32_t millis();\nuint32_t micros();\n')
common='#include <cassert>\n#include <cstdio>\n#include <cstring>\n#include <vector>\n#include <cstdint>\n'
def run(name,code,extra=[]):
 f=w/(name+'.cpp');f.write_text(common+code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(w),'-I'+str(sketch),str(f),*map(str,extra),'-o',str(w/name)],check=True)
 subprocess.run([str(w/name)],check=True)
run('transport',r'''
#include "BridgeTransport.h"
uint32_t now=20;uint32_t millis(){return now;}uint32_t micros(){return now*1000;}
struct Frame{uint8_t code;std::vector<char> bytes;};std::vector<Frame> sent;
uint8_t uart_send_message(char c,char* p,uint8_t n){sent.push_back({uint8_t(c),std::vector<char>(p,p+n)});return 1;}
uint16_t id(){auto& p=sent.back().bytes;return uint8_t(p[0])|(uint16_t(uint8_t(p[1]))<<8);}
void reply(uint16_t id,uint8_t status,bool ready=true){
 assert(sent.back().code==3);char p[18]={sent.back().bytes[0],2,char(ready),char(id),char(id>>8),char(status),1,0,2,0,3,0,4,0,5,0,80,0};BridgeTransport::receive(p,sizeof(p));
}
int main(){using namespace BridgeTransport;
 process(false,true);reply(90,2);assert(ready());char p[]={char(128),20,32,1,1};
 assert(start(NORMAL,p,5,100));uint16_t first=id();assert(first==91 && sent.back().code==5 && !ready());
 assert(!start(FLASH,p,5,100));now+=20;process(false,true);reply(first,1,false);assert(diagnostics().accepted==1);
 now+=20;process(false,true);reply(first,2);assert(take(NORMAL)==TRANSMITTED && !busy());
 assert(diagnostics().avrQueueDrops==2 && diagnostics().avrStackMinFree==80);
 assert(start(NORMAL,p,5,100));uint16_t second=id();cancel();assert(sent.back().code==7);
 now+=20;process(false,true);reply(second,3);assert(take(NORMAL)==EXPIRED);
 assert(start(NORMAL,p,5,100));uint16_t third=id();now+=20;process(false,true);reply(third,4);assert(take(NORMAL)==REJECTED);
 assert(start(NORMAL,p,5,100));now+=1100;process(false,true);assert(take(NORMAL)==UNKNOWN && !ready());
 size_t before=sent.size();now+=1000;process(true,true);assert(sent.size()==before && !start(FLASH,p,5,100));
 now+=1000;process(false,true);reply(third,4);p[1]=0;p[2]=3;assert(start(NORMAL,p,5,100));legacyBootStarted();assert(take(NORMAL)==TRANSMITTED);
 int submissions=0;for(auto& f:sent)if(f.code==5)++submissions;assert(submissions==5); // No retransmission of uncertain commands.
 puts("PASS: UART v2 admission/results/cancel/unknown, no command retry, legacy pause, remote diagnostics");
}
''',[sketch/'BridgeTransport.cpp'])
# Actual driver timer ISR and admission routines; CPU pin/timer registers are simulated.
code=r'''
#define CLUNET_SENDING_STATE_IDLE 0
#define CLUNET_SENDING_STATE_INIT 1
#define CLUNET_SENDING_STATE_PRIO1 2
#define CLUNET_SENDING_STATE_PRIO2 3
#define CLUNET_SENDING_STATE_DATA 4
#define CLUNET_SENDING_STATE_PREPARING 5
#define CLUNET_SENDING_STATE_WAITING_LINE 6
#define CLUNET_SENDING_STATE_PREINIT 7
#define CLUNET_SENDING_STATE_STOP 8
#define CLUNET_SENDING_STATE_DONE 9
#define CLUNET_READING_STATE_IDLE 0
#define CLUNET_PRIORITY_MESSAGE 3
#define CLUNET_MULTICAST_DEVICE(a) ((a)&128)
#define CLUNET_OFFSET_SRC_ADDRESS 0
#define CLUNET_OFFSET_DST_ADDRESS 1
#define CLUNET_OFFSET_COMMAND 2
#define CLUNET_OFFSET_SIZE 3
#define CLUNET_OFFSET_DATA 4
#define CLUNET_SEND_BUFFER_SIZE 133
#define CLUNET_TIMER_REG timerReg
#define CLUNET_TIMER_REG_OCR timerOcr
#define CLUNET_T 16
#define CLUNET_INIT_T 64
#define CLUNET_1_T 48
#define CLUNET_0_T 16
#define CLUNET_SEND_0 (sending=false)
#define CLUNET_SEND_INVERT (sending=!sending)
#define CLUNET_SENDING sending
#define CLUNET_READING reading
#define CLUNET_ENABLE_TIMER_COMP (timerEnabled=true)
#define CLUNET_DISABLE_TIMER_COMP (timerEnabled=false)
#define ISR(v) void v()
#define UART_MESSAGE_CODE_CLUNET 1
unsigned char SREG=128,clunetSendingState=0,clunetReadingState=0,clunetCurrentPrio=0,clunetSendingCurrentByte=0,clunetSendingCurrentBit=0;
unsigned char clunetTrackedResult=0,clunetTrackedActive=0,clunetExpirePending=0,timerReg=0,timerOcr=0;
unsigned clunetSendingDataLength=0;char dataToSend[133];bool sending=false,reading=false,timerEnabled=false;
void cli(){SREG=0;}
'''
for signature in ['char check_crc(', 'static void clunet_finish_tracked(', 'ISR(CLUNET_TIMER_COMP_VECTOR)', 'void clunet_start_send(', 'static void clunet_prepare_packet(', 'static unsigned char clunet_try_send_mode(', 'unsigned char clunet_try_send_tracked(', 'void clunet_expire_tracked(']:code+=fun(driver,signature)+'\n'
code+=r'''
unsigned char systime=0;
uint16_t uart_hardware_errors=1,clunet_queue_drops=2,uart_overflows=3,legacy_expired=0,uart_crc_errors=4;
uint16_t stack_watermark(){return 80;}
char uart_ready_to_send(){return 1;}
std::vector<char> response;
char uart_send_message(char code,char* p,unsigned char n){assert(code==4);response.assign(p,p+n);return 1;}
void discovery_listen_header(unsigned char,unsigned char){}
'''
chunk=avr[avr.index('static unsigned int bridge_now'):avr.index('// Alternate credit replies')]
code+=chunk.replace('unsigned int','uint16_t')+r'''
void tick(int n){for(int i=0;i<n;i++){++systime;service_transport();}}
int main(){
 char p[]={1,0,100,0,char(128),20,32,1,7};clunetReadingState=1;
 assert(on_uart_message(5,p,9));assert(tx_status==1 && clunetSendingState==6);
 on_uart_message(5,p,9);assert(tx_status==1);tick(100);assert(tx_status==3 && clunetSendingState==0);
 on_uart_message(5,p,9);assert(tx_status==3); // Duplicate expired id is never executed again.
 p[0]=2;clunetReadingState=0;on_uart_message(5,p,9);assert(tx_status==1);
 clunetSendingState=CLUNET_SENDING_STATE_DATA;tick(100);assert(clunetSendingState==4 && clunetExpirePending);
 clunetSendingState=CLUNET_SENDING_STATE_DONE;CLUNET_TIMER_COMP_VECTOR();service_transport();assert(tx_status==2); // Current frame can finish.
 p[0]=3;clunetReadingState=1;on_uart_message(5,p,9);char cancel[]={3,0};on_uart_message(7,cancel,2);service_transport();assert(tx_status==3);
 p[0]=4;clunetReadingState=0;on_uart_message(5,p,9);clunetSendingState=4;tick(100);
 clunetSendingState=6;clunet_start_send();service_transport();assert(tx_status==3 && clunetSendingState==0); // No retry after deadline.
 p[0]=5;p[2]=0;on_uart_message(5,p,9);assert(tx_status==4);
 char nonce=42;on_uart_message(3,&nonce,1);assert(response.size()==18 && response[0]==42 && response[1]==2 && response[6]==1 && response[16]==80);
 char legacy[]={char(128),20,32,1,1};clunetSendingState=1;assert(!on_uart_message(1,legacy,5));tick(2000);assert(on_uart_message(1,legacy,5) && legacy_expired==1);
 // 16-bit bridge time wrap must not extend a queued command's lifetime.
 clunetSendingState=0;clunetReadingState=1;bridge_now=65530;p[0]=6;p[2]=100;on_uart_message(5,p,9);tick(100);assert(tx_status==3);
 puts("PASS: real AVR ISR, expiry before start, finish current frame, suppress collision retry, cancel/idempotency, legacy wait expiry, 16-bit wrap");
}
'''
run('avr',code)
# Host-side flash implementation, unchanged legacy frame bytes, strict HEX checks and terminal states.
constants=flash[flash.index('static constexpr uint8_t UART_MESSAGE_CODE_CLUNET'):flash.index('static const __FlashStringHelper*')]
code=r'''
#include "BootloaderLease.h"
#include "LegacyFlashFlow.h"
#define CLUNET_COMMAND_BOOT_CONTROL 2
#define CLUNET_COMMAND_REBOOT 3
#define CLUNET_COMMAND_BOOT_COMPLETED 4
#define CLUNET_COMMAND_PING 254
#define CLUNET_COMMAND_PING_REPLY 255
#define CLUNET_ADDRESS_BROADCAST 255
uint32_t clockMs=0;uint32_t millis(){return clockMs;}uint32_t micros(){return clockMs*1000;}
struct AsyncWebServerRequest{};
struct clunet_packet{uint8_t src,dst,command,size;char data[8];};
namespace BridgeTransport {
 enum Result:uint8_t{NO_RESULT,PENDING,TRANSMITTED,EXPIRED,REJECTED,UNKNOWN};enum Owner{NONE,NORMAL,FLASH,PROBE};
 Result scripted=NO_RESULT;int starts=0,cancels=0;std::vector<char> last;
 struct {uint8_t protocol=0;uint32_t lastReplyAt=0;} diag;
 auto& diagnostics(){return diag;}
 bool start(Owner,const char* p,uint8_t n,uint16_t){++starts;last.assign(p,p+n);return true;}
 Result take(Owner o){if(o==PROBE)return NO_RESULT;Result r=scripted;scripted=NO_RESULT;return r;}
 void cancel(){++cancels;}
}
int rawFrames=0;std::vector<char> raw;
uint8_t uart_can_send(uint8_t){return 1;}
uint8_t uart_send_message(char code,char* p,uint8_t n){assert(code==1);raw.assign(p,p+n);++rawFrames;return 1;}
'''+constants
for signature in ['static bool flashParseUnsigned(', 'static bool flashStageHasTimeout(', 'static void flashSetStatus(', 'static void flashSetUploadError(', 'static void flashSetSessionError(', 'static void flashResetUploadState(', 'static int8_t flashHexNibble(', 'static bool flashHexByte(', 'static bool flashProcessHexLine(', 'static void flashUploadConsumeChunk(', 'static void flashUploadFinalize(', 'static bool flashSendClunetPacket(', 'bool handleBootControlResponse(', 'static void flashProcessSession(', 'void observeApplicationPacket(']:code+=fun(flash,signature)+'\n'
code+=r'''
bool line(const char* p){return flashProcessHexLine(p,strlen(p));}
void state(uint8_t target){memset(&flashSession,0,sizeof(flashSession));flashSession.active=true;flashSession.target=target;flashSession.applicationLimit=7168;flashSession.stage=FLASH_STAGE_SEND_DONE;}
int main(){
 uint16_t number=99;assert(flashParseUnsigned("0",127,number) && number==0);assert(!flashParseUnsigned("abc",127,number));assert(!flashParseUnsigned("128",127,number));assert(!flashParseUnsigned("-1",127,number));assert(!flashParseUnsigned("99999999999999999",127,number));
 flashResetUploadState();assert(line(":020000000C945E"));assert(line(":00000001FF"));flashUploadFinalize();assert(flashFirmwareReady && flashFirmwareLength==2 && firmwareCrc32);
 assert(!line(":020002000C945C"));
 flashResetUploadState();assert(line(":020000000C945E"));assert(!line(":020000000D945D"));assert(flashUpload.hasError);
 flashResetUploadState();assert(line(":02000004FFFFFC"));assert(!line(":02FFFF000102FD"));
 flashResetUploadState();assert(line(":00000001FF"));flashUploadFinalize();assert(!flashFirmwareReady);
 flashResetUploadState();assert(line(":020000000C945E"));flashUploadFinalize();assert(!flashFirmwareReady);
 assert(!LegacyFlashFlow::validPageSize(0) && !LegacyFlashFlow::validPageSize(63) && LegacyFlashFlow::validPageSize(64));
 LegacyFlashFlow flow;char init[]={1},ready[]={2,64,0},write[68]={3,0,64,0},ack[]={4},done[]={5};
 assert(flow.allows(init,1,0));flow.submitted(init,1,0);assert(!flow.allows(init,1,1));flow.response(ready,3);
 assert(flow.allows(write,68,1));flow.submitted(write,68,1);assert(!flow.allows(write,68,2));flow.response(ack,1);assert(!flow.allows(write,68,3));
 write[1]=1;assert(flow.allows(write,68,3));write[2]=65;assert(!flow.allows(write,68,3));assert(flow.allows(done,1,3));
 state(20);clockMs=10;flashProcessSession();assert(flashSession.stage==FLASH_STAGE_WAIT_DONE && flashSession.active && !flashSession.applicationResponded);
 BridgeTransport::scripted=BridgeTransport::TRANSMITTED;flashProcessSession();assert(flashSession.stage==FLASH_STAGE_VERIFY);
 clockMs+=6000;flashProcessSession();assert(flashSession.stage==FLASH_STAGE_UNCONFIRMED && !flashSession.active);
 state(20);flashProcessSession();BridgeTransport::scripted=BridgeTransport::TRANSMITTED;flashProcessSession();
 clunet_packet packet={20,238,255,4,{}};memcpy(packet.data,&flashSession.probeToken,4);observeApplicationPacket(&packet);flashProcessSession();assert(flashSession.stage==FLASH_STAGE_DONE && flashSession.applicationResponded);
 state(0);legacyApplicationSeen=false;flashProcessSession();assert(rawFrames==1 && raw.size()==5 && raw[0]==char(238) && raw[1]==0 && raw[2]==2 && raw[4]==5);
 int before=BridgeTransport::starts;clockMs+=6000;flashProcessSession();assert(flashSession.stage==FLASH_STAGE_UNCONFIRMED && BridgeTransport::starts==before);
 state(20);flashProcessSession();BridgeTransport::scripted=BridgeTransport::EXPIRED;flashProcessSession();assert(flashSession.stage==FLASH_STAGE_ERROR);
 puts("PASS: HEX CRC/overlap/overflow/EOF/vector, page and stop-wait guards, unchanged legacy UART DONE, transmitted vs application response vs unconfirmed/error");
}
'''
run('flash',code)
