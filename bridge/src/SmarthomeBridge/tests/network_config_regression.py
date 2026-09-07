#!/usr/bin/env python3
"""Check the real persisted-settings codec and the AP/config integration contract."""
from pathlib import Path
import re
import subprocess
import tempfile

sketch = Path(__file__).resolve().parents[1]
source = (sketch / 'NetworkConfig.cpp').read_text()
ino = (sketch / 'SmarthomeBridge.ino').read_text()
page = (sketch / 'data/www/config.html').read_text()


def declaration(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    while end < len(source) and source[end] in ';\n':
        end += 1
    return source[start:end]


work = Path(tempfile.mkdtemp(prefix='bridge-network-config-'))
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdlib>
class IPAddress {
  uint8_t bytes[4] = {};
public:
  IPAddress() = default;
  IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d){bytes[0]=a;bytes[1]=b;bytes[2]=c;bytes[3]=d;}
  bool fromString(const char* value){
    unsigned v[4];char tail;
    if(!value || std::sscanf(value,"%u.%u.%u.%u%c",&v[0],&v[1],&v[2],&v[3],&tail)!=4)return false;
    for(int i=0;i<4;i++){if(v[i]>255)return false;bytes[i]=v[i];}return true;
  }
  bool operator!=(const IPAddress& other)const{return std::memcmp(bytes,other.bytes,4)!=0;}
};
static constexpr uint32_t SETTINGS_MAGIC = 0x53484257UL;
static constexpr uint16_t SETTINGS_VERSION = 1;
static constexpr uint32_t TIMEZONE_MAGIC = 0x5348545AUL;
static constexpr uint16_t TIMEZONE_VERSION = 1;
struct TimeZoneOption {};
static const TimeZoneOption TEST_ZONE = {};
static const TimeZoneOption* findTimeZoneById(const char* id){
  return id && (!std::strcmp(id,"Europe/Samara") || !std::strcmp(id,"UTC")) ? &TEST_ZONE : nullptr;
}
'''
for signature in ['struct __attribute__((packed)) StoredSettings',
                  'struct __attribute__((packed)) StoredTimeZone',
                  'static uint32_t crc32(', 'static uint32_t settingsCrc(',
                  'static uint32_t timeZoneCrc(', 'static bool parseIp(',
                  'static bool settingsValid(', 'static bool timeZoneValid(']:
    code += declaration(signature) + '\n'
code += r'''
int main(){
  StoredSettings value={};
  assert(!settingsValid(value));
  value.magic=SETTINGS_MAGIC;value.version=SETTINGS_VERSION;
  std::strcpy(value.ssid,"home");std::strcpy(value.password,"12345678");
  value.crc=settingsCrc(value);assert(settingsValid(value));
  value.ssid[0]^=1;assert(!settingsValid(value));value.ssid[0]^=1;
  value.useStaticIp=1;value.crc=settingsCrc(value);assert(!settingsValid(value));
  std::strcpy(value.ip,"192.168.50.243");std::strcpy(value.gateway,"192.168.50.1");
  std::strcpy(value.subnet,"255.255.255.0");value.crc=settingsCrc(value);assert(settingsValid(value));
  std::strcpy(value.dns,"999.1.1.1");value.crc=settingsCrc(value);assert(!settingsValid(value));
  std::strcpy(value.dns,"192.168.50.1");value.crc=settingsCrc(value);assert(settingsValid(value));
  value.password[sizeof(value.password)-1]='x';value.crc=settingsCrc(value);assert(!settingsValid(value));
  value.password[sizeof(value.password)-1]=0;value.crc=settingsCrc(value);assert(settingsValid(value));
  StoredTimeZone zone={};assert(!timeZoneValid(zone));
  zone.magic=TIMEZONE_MAGIC;zone.version=TIMEZONE_VERSION;std::strcpy(zone.id,"Europe/Samara");
  zone.crc=timeZoneCrc(zone);assert(timeZoneValid(zone));
  std::strcpy(zone.id,"Bad/Zone");zone.crc=timeZoneCrc(zone);assert(!timeZoneValid(zone));
  puts("PASS: settings CRC, corruption detection, network/timezone validation and terminators");
}
'''
(work / 'network.cpp').write_text(code)
subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', str(work / 'network.cpp'), '-o', str(work / 'network')], check=True)
subprocess.run([str(work / 'network')], check=True)

assert 'const char *ssid' not in ino and 'const char *pass' not in ino
assert 'NetworkConfig::process();' in ino
assert 'NetworkConfig::stationConnected()' in ino
assert 'passwordConfigured' in source
assert not re.search(r'wifi\[\s*"password"\s*\]\s*=\s*settings', source)
assert 'dnsServer.processNextRequest();' in source
assert 'now - stateStartedAt >= CONNECT_TIMEOUT_MS' in source
assert 'transitionAllowed()' in source and 'scheduledApAt' in source
assert 'FALLBACK_PAGE' in source and '/www/config.html' in source
assert 'background: #f4f6f8' in page and 'Настройки SmarthomeBridge' in page
assert 'id="apPassword"' in page and 'id="deviceTime"' in page
assert 'id="timeZone"' in page and '/api/timezones' in page
assert "timeZone: byId('timeZone').value" in page
assert 'configTime(getPosixTimeZone(zone->key)' in source
script = re.search(r'<script>(.*)</script>', page, re.S)
assert script
subprocess.run(['node', '--check', '-'], input=script.group(1), text=True, check=True)
print('PASS: no home credentials, 60 s fallback, captive DNS, white config UI, timezone persistence and STA traffic gating')
