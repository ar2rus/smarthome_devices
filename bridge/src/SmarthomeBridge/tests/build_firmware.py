#!/usr/bin/env python3
"""Build both bridge firmwares with installed toolchains; never upload to hardware."""
import argparse,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,default=None)
p.add_argument('--arduino-cli',default='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli')
p.add_argument('--arduino-data',type=Path,default=Path.home()/'Library/Arduino15')
p.add_argument('--library',type=Path,default=Path.home()/'Documents/Arduino/libraries/ClunetMulticast')
p.add_argument('--avr-bin',type=Path,default=Path.home()/'Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin')
p.add_argument('--fqbn',default='esp8266:esp8266:generic:xtal=80,eesz=4M1M,ip=hb2f')
a=p.parse_args();root=Path(__file__).resolve().parents[4];out=a.output or Path(tempfile.mkdtemp(prefix='bridge-build-'));out.mkdir(parents=True,exist_ok=True)
config=out/'arduino.yaml';config.write_text(json.dumps({'directories':{'data':str(a.arduino_data),'downloads':str(out/'downloads'),'user':str(Path.home()/'Documents/Arduino')},'build_cache':{'path':str(out/'cache')}}))
def run(label,cmd):
 with (out/(label+'-build.log')).open('w') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 print(label+': '+('PASS' if r.returncode==0 else 'FAIL'),flush=True)
 if r.returncode:print((out/(label+'-build.log')).read_text()[-8000:])
 return r.returncode
esp=run('esp',[a.arduino_cli,'compile','--config-file',str(config),'--fqbn',a.fqbn,'--warnings','all','--library',str(a.library),'--build-path',str(out/'esp-build'),'--jobs','4',str(root/'bridge/src/SmarthomeBridge')])
avr=run('avr',[str(a.avr_bin/'avr-gcc'),'-mmcu=atmega8','-O2','-std=gnu99','-DF_CPU=16000000UL','-funsigned-char','-funsigned-bitfields','-fpack-struct','-fshort-enums','-Wall','-I'+str(root/'_lib'),'-I'+str(root/'bridge/src/Bridge'),'-I'+str(root/'bridge/src/Bridge/clunet'),str(root/'bridge/src/Bridge/Bridge.c'),str(root/'_lib/clunet/clunet.c'),str(root/'_lib/clunet/clunet_buffered.c'),'-o',str(out/'Bridge.elf')])
for line in (out/'esp-build.log').read_text().splitlines():
 if 'used ' in line and not 'warning:' in line:print(line)
if not avr:
 print(subprocess.check_output([str(a.avr_bin/'avr-size'),'-C','--mcu=atmega8',str(out/'Bridge.elf')],text=True))
 subprocess.run([str(a.avr_bin/'avr-objcopy'),'-O','ihex','-R','.eeprom',str(out/'Bridge.elf'),str(out/'Bridge.hex')],check=True)
print('Build output:',out)
raise SystemExit(1 if esp or avr else 0)
