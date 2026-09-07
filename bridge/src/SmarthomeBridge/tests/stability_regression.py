#!/usr/bin/env python3
"""Compile actual parser/codec/FIFO/AVR admission code with sanitizers; no device IO."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--library', type=Path, default=Path.home() / 'Documents/Arduino/libraries/ClunetMulticast/src')
parser.add_argument('--bridge', type=Path)
parser.add_argument('--devices', type=Path, default=Path(__file__).resolve().parents[4])
args = parser.parse_args()
sketch = Path(__file__).resolve().parents[1]
root = Path(__file__).resolve().parents[4]
work = Path(tempfile.mkdtemp(prefix='bridge-regression-'))

def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

ino = (sketch / 'SmarthomeBridge.ino').read_text()
avr = (args.bridge or args.devices / 'bridge/src/Bridge/Bridge.c').read_text()
header = (args.library / 'ClunetMulticast.h').read_text()
packet = header[header.index('struct clunet_packet{'):header.index('struct clunet_response{')]
code = '''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <new>
#include "BoundedQueue.h"
#include "MessageDecoder.h"
#include "clunet_buffered.h"
#define CLUNET_PACKET_DATA_SIZE 128
'''+packet+'''
#define UART_RX_BUF_LENGTH 256
const char UART_MESSAGE_PREAMBULE[]={char(0xC9),char(0xE7)};
volatile uint16_t uart_rx_data_len=0;
char uart_rx_data[256];
uint32_t clockMs=0, uartLastRxAt=0, uartCrcErrors=0, uartTimeouts=0;
uint32_t millis(){ return clockMs; }
'''
for signature in ['char check_crc(', 'void analyze_uart_rx_trim(', 'void analyze_uart_rx(']:
    code += function(ino, signature)+'\n'
code += '\n#undef UART_RX_BUF_LENGTH\n#define UART_RX_BUF_LENGTH 96\nnamespace avr_parser {\n' + '''
unsigned char systime=0, uart_rx_last_at=0, uart_rx_data_len=0, uart_rx_overflow=0, SREG=0;
char uart_rx_data[96]; unsigned int uart_crc_errors=0;
void cli(){}
''' + '\n'.join(function(avr, f) for f in ['void uart_rx_reset(', 'void analyze_uart_rx_trim(', 'void analyze_uart_rx(']) + '\n}\n'
code += '''
#define CLUNET_SENDING_STATE_IDLE 0
#define CLUNET_PRIORITY_MESSAGE 3
#define UART_MESSAGE_CODE_CLUNET 1
#define CLUNET_MULTICAST_DEVICE(a) ((a)&128)
unsigned char clunetSendingState=0,clunetTrackedResult=0,SREG=0,systime=0;
unsigned int uart_hardware_errors=0,clunet_queue_drops=0,uart_overflows=0,legacy_expired=0,uart_crc_errors=0;
void cli(){}
void clunet_expire_tracked(){clunetTrackedResult=3;clunetSendingState=0;}
unsigned int stack_watermark(){return 80;}
int sends=0, grants=0;
char uart_ready_to_send(){ return 1; }
char uart_send_message(char code,char* data,unsigned char length){
  assert(code==4 && length==18 && data[1]==2); grants += data[2]; return 1;
}
void discovery_listen_header(unsigned char,unsigned char){}
void discovery_start_if_request(unsigned char,unsigned char){}
unsigned char clunet_try_send_tracked(unsigned char,unsigned char,unsigned char priority,unsigned char,char*,unsigned char){
  assert(priority==CLUNET_PRIORITY_MESSAGE); if(clunetSendingState) return 0;
  ++sends; clunetSendingState=1; return 1;
}
'''+avr[avr.index('static unsigned int bridge_now'):avr.index('// Alternate credit replies')]+'''
int decoded=0;
void onFrame(uint8_t, char*, uint8_t){ ++decoded; }
int released=0;
void release(int* value){ ++released; delete value; }
int main(){
  // Boundary lengths and malformed input must never write beyond supplied capacities.
  for(size_t len=0;len<=128;++len){
    char bytes[128]={}, hex[257]={}, result[128]={};
    for(size_t i=0;i<len;++i) bytes[i]=i;
    assert(charArrayToHexString(hex,bytes,len,sizeof(hex))==int(2*len));
    assert(hexStringToCharArray(result,hex,2*len,sizeof(result))==int(len));
    assert(!memcmp(bytes,result,len));
    if(len) assert(charArrayToHexString(hex,bytes,len,2*len)==-1);
  }
  char out[128]={};
  assert(hexStringToCharArray(out,"fF",2,sizeof(out))==1 && (uint8_t)out[0]==255);
  assert(hexStringToCharArray(out,"F",1,sizeof(out))==-1);
  assert(hexStringToCharArray(out,"G0",2,sizeof(out))==-1);
  char shortPacket[4]={1,128,1,64};
  assert(!clunet_packet::valid(shortPacket,sizeof(shortPacket)));
  shortPacket[3]=0; assert(clunet_packet::valid(shortPacket,4));
  assert(!clunet_packet::valid(shortPacket,3));
  alignas(float) char decodedTemperature[512];
  char sensors[89]={}; sensors[0]=22;
  for(int i=0;i<22;i++) sensors[1+4*i]=1;
  assert(!getTemperatureInfo(sensors,sizeof(sensors),decodedTemperature,sizeof(decodedTemperature)));
  char validSensor[]={1,1,1,100,0};
  assert(getTemperatureInfo(validSensor,sizeof(validSensor),decodedTemperature,sizeof(decodedTemperature)));
  assert(!getTemperatureInfo(validSensor,4,decodedTemperature,sizeof(decodedTemperature)));
  char humidity[2]={}; assert(!getHumidityInfo(humidity,1,decodedTemperature,sizeof(decodedTemperature)));

  // Exercise every wrap location against the actual FIFO implementation.
  for(int offset=0;offset<512;offset++){
    clunet_buffered_init();
    for(int i=0;i<offset;i++){ assert(clunet_buffered_push(1,2,3,nullptr,0)); clunet_buffered_pop(); }
    for(int i=0;i<4;i++){ char value=i; assert(clunet_buffered_push(1,2,3,&value,1)); }
    assert(!clunet_buffered_push(1,2,3,nullptr,0));
    for(int i=0;i<4;i++){ assert(clunet_buffered_peek()->data[0]==i); clunet_buffered_pop(); }
    assert(clunet_buffered_is_empty());
  }
  {
    BoundedQueue<int*,2> q(release);
    assert(q.add(new int(1),UINT32_MAX-4)); assert(q.age(5)==10);
    assert(q.add(new int(2),0)); int* rejected=new int(3);
    assert(!q.add(rejected,0)); delete rejected;
    q.remove(q.front()); assert(*q.front()==2); q.clear(); assert(released==2);
  }
  // Concatenated frames cross the old 256-byte boundary, with immediate incremental parsing.
  char frame[]={char(0xC9),char(0xE7),3,10,0}; frame[4]=check_crc(frame+2,2);
  for(int n=0;n<200;n++) for(char b:frame){
    uart_rx_data[uart_rx_data_len++]=b; analyze_uart_rx(onFrame);
  }
  assert(decoded==200 && uart_rx_data_len==0);
  char partial[]={char(0xC9),char(0xE7),74,1,0};
  memcpy(uart_rx_data,partial,5); memcpy(uart_rx_data+5,frame,5); uart_rx_data_len=10;
  analyze_uart_rx(onFrame); assert(decoded==200);
  clockMs=101; analyze_uart_rx(onFrame); assert(decoded==201 && uartTimeouts==1);
  frame[4]^=1; memcpy(uart_rx_data,frame,5); uart_rx_data_len=5;
  analyze_uart_rx(onFrame); assert(uartCrcErrors==1);
  // AVR partial frames recover after 100 ms, including systime wrap.
  frame[4]^=1;
  memcpy(avr_parser::uart_rx_data,partial,5); memcpy(avr_parser::uart_rx_data+5,frame,5);
  avr_parser::uart_rx_data_len=10; avr_parser::uart_rx_last_at=230; avr_parser::systime=230;
  avr_parser::analyze_uart_rx(nullptr); assert(avr_parser::uart_rx_data_len==10);
  avr_parser::systime=75; avr_parser::analyze_uart_rx(nullptr); assert(avr_parser::uart_rx_data_len==0);
  // An incoming CLUNET frame stays pending while TX is busy, without clobbering it.
  char message[]={char(128),20,32,1,1};
  clunetSendingState=1; assert(!on_uart_message(1,message,5) && sends==0);
  clunetSendingState=0; assert(on_uart_message(1,message,5) && sends==1);
  assert(!on_uart_message(1,message,5) && sends==1);
  message[3]=2; assert(on_uart_message(1,message,5) && sends==1);
  char nonce=42; clunetSendingState=1; assert(on_uart_message(3,&nonce,1) && grants==0);
  clunetSendingState=0; clunetTrackedResult=2; assert(on_uart_message(3,&nonce,1) && grants==1);
  puts("PASS: codecs, packet validation, sensor bounds, 512 FIFO wrap positions, bounded queues, UART burst/resync/CRC, AVR busy admission and credit");
}
'''
(work/'regression.cpp').write_text(code)
command = ['clang++','-std=c++17','-O1','-g','-funsigned-char','-fsanitize=address,undefined','-fno-sanitize-recover=all',
           '-I'+str(sketch),'-I'+str(args.library),'-I'+str(args.devices/'_lib/clunet'),
           '-I'+str(root/'bridge/src/Bridge/clunet'),str(work/'regression.cpp'),
           str(args.library/'HexUtils.cpp'),'-x','c++',str(args.devices/'_lib/clunet/clunet_buffered.c'),
           '-o',str(work/'regression')]
subprocess.run(command,check=True)
subprocess.run([str(work/'regression')],check=True)
print('Artifacts:',work)
