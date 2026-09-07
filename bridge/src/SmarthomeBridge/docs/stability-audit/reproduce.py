#!/usr/bin/env python3
"""Read-only host reproductions of current bridge source defects. No device I/O."""
from pathlib import Path
import json
import re
import subprocess
import argparse
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[5])
parser.add_argument('--library', type=Path, default=Path.home() / 'Documents/Arduino/libraries/ClunetMulticast/src')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
OUT = args.output or Path(tempfile.mkdtemp(prefix='smarthome-stability-repro-'))
OUT.mkdir(parents=True, exist_ok=True)
ROOT = args.root
SKETCH = ROOT / 'bridge/src/SmarthomeBridge'
LIB = args.library
print('Results directory:', OUT)

def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

ino = (SKETCH / 'SmarthomeBridge.ino').read_text()
driver = (ROOT / '_lib/clunet/clunet.c').read_text()
packet_header = (LIB / 'ClunetMulticast.h').read_text()
packet = packet_header[packet_header.index('struct clunet_packet{'):packet_header.index('struct clunet_response{')]
source = r'''
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "clunet_buffered.h"
#include "MessageDecoder.h"
'''
source += packet + '\n'
source += '#define CLUNET_SENDING_STATE_IDLE 0\nunsigned char clunetSendingState=1, clunetCurrentPrio=0;\n'
source += function(driver, 'int clunet_ready_to_send()') + '\n'
source += '#define UART_RX_BUF_LENGTH 256\nconst char UART_MESSAGE_PREAMBULE[]={char(0xC9),char(0xE7)};\nvolatile char uart_rx_data[256];\nvolatile unsigned char uart_rx_data_len=0;\nbool uart_rx_overflow=false;\n'
source += function(ino, 'char check_crc(') + '\n'
source += function(ino, 'void analyze_uart_rx_trim(') + '\n'
source += function(ino, 'void analyze_uart_rx(') + '\n'
source += 'struct SerialStub { int availableForWrite() { return 128; } } Serial;\n'
source += function(ino, 'uint8_t uart_can_send(uint8_t length)') + '\n'
source += r'''
int frames=0;
void countFrame(uint8_t, char*, uint8_t) { ++frames; }
int main(int argc, char** argv) {
  std::string test=argc>1 ? argv[1] : "";
  if(test=="uart_counter") {
    for(int n=0;n<256;n++) {
      char byte=0;
      if(uart_rx_data_len<UART_RX_BUF_LENGTH) uart_rx_data[uart_rx_data_len++]=byte;
      else uart_rx_overflow=true;
    }
    printf("after 256 bytes: buffered=%u overflow=%d\n",uart_rx_data_len,uart_rx_overflow);
    return !(uart_rx_data_len==0 && !uart_rx_overflow);
  }
  if(test=="fifo_wrap") {
    FIFO(4) fifo={};
    for(int n=0;n<252;n++) { FIFO_PUSH(fifo); FIFO_POP(fifo); }
    for(int n=0;n<4;n++) FIFO_PUSH(fifo);
    printf("full queue after wrap: head=%u tail=%u count=%d full=%d\n",fifo.head,fifo.tail,FIFO_COUNT(fifo),FIFO_IS_FULL(fifo));
    return FIFO_IS_FULL(fifo);
  }
  if(test=="priority_zero") {
    printf("busy state=%u priority=%u ready_to_send=%d (0 means ready)\n",clunetSendingState,clunetCurrentPrio,clunet_ready_to_send());
    return clunet_ready_to_send();
  }
  if(test=="filter_sentinel") {
    uint8_t responseFilterCommand=-1;
    bool matches=(responseFilterCommand<0 || 0x61==responseFilterCommand);
    printf("filter -1 stored=%u matches 0x61=%d\n",responseFilterCommand,matches);
    return matches;
  }
  if(test=="uart_tx_head_block") {
    printf("empty TX FIFO: payload119=%u payload120=%u payload128=%u\n",uart_can_send(123),uart_can_send(124),uart_can_send(132));
    return !(uart_can_send(123) && !uart_can_send(124) && !uart_can_send(132));
  }
  if(test=="uart_stalled_partial") {
    // A false preamble with plausible length precedes a complete valid empty frame.
    char bytes[]={char(0xC9),char(0xE7),90,1,0,char(0xC9),char(0xE7),3,1,0};
    bytes[9]=check_crc(bytes+7,2);
    memcpy((void*)uart_rx_data,bytes,sizeof(bytes));
    uart_rx_data_len=sizeof(bytes);
    analyze_uart_rx(countFrame);
    analyze_uart_rx(countFrame);
    printf("valid trailing frame blocked: callbacks=%d buffered=%u\n",frames,uart_rx_data_len);
    return frames!=0;
  }
  if(test=="hex_decode_128") {
    char input[257]; memset(input,'A',256); input[256]=0;
    char output[256];
    hexStringToCharArray(output,input,256);
    return 0;
  }
  if(test=="hex_encode_128") {
    char input[128]={}; char output[256];
    charArrayToHexString(output,input,128);
    return 0;
  }
  if(test=="temperature_22") {
    char input[89]={}; input[0]=22;
    for(int i=0;i<22;i++) { input[1+i*4]=1; input[2+i*4]=1; }
    alignas(float) char output[512];
    getTemperatureInfo(input,output);
    return 0;
  }
  if(test=="packet_inner_size") {
    char input[4]={1,128,1,64};
    auto copy=reinterpret_cast<clunet_packet*>(input)->copy();
    delete[] reinterpret_cast<char*>(copy);
    return 0;
  }
  return 2;
}
'''
# The ESP8266 compiler reports __CHAR_UNSIGNED__=1; match that on the host.
(OUT/'repro.cpp').write_text(source)
cmd=['clang++','-std=c++17','-O0','-g','-funsigned-char','-fsanitize=address,undefined',
     '-fno-omit-frame-pointer','-I'+str(ROOT/'_lib/clunet'),
     '-I'+str(ROOT/'bridge/src/Bridge/clunet'),'-I'+str(LIB),
     str(OUT/'repro.cpp'),str(LIB/'HexUtils.cpp'),'-o',str(OUT/'repro')]
build=subprocess.run(cmd,text=True,capture_output=True)
(OUT/'repro-build.log').write_text(build.stdout+build.stderr)
if build.returncode:
    raise SystemExit(build.stderr)
results=[]
for name in ['uart_counter','fifo_wrap','priority_zero','filter_sentinel','uart_tx_head_block','uart_stalled_partial',
             'hex_decode_128','hex_encode_128','temperature_22','packet_inner_size']:
    p=subprocess.run([str(OUT/'repro'),name],text=True,capture_output=True,timeout=5)
    log=p.stdout+p.stderr
    (OUT/(name+'.log')).write_text(log)
    issue=next((line for line in log.splitlines() if 'ERROR: AddressSanitizer:' in line),None)
    detail=issue or p.stdout.strip()
    confirmed=(p.returncode!=0 and issue is not None) if name in ['hex_decode_128','hex_encode_128','temperature_22','packet_inner_size'] else p.returncode==0
    results.append({'test':name,'confirmed':confirmed,'detail':detail})
    print(name+': '+str(confirmed)+' '+detail)
(OUT/'repro-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2))

if not all(row['confirmed'] for row in results):
    raise SystemExit('Some historical defects were not reproduced; review source changes and logs.')
