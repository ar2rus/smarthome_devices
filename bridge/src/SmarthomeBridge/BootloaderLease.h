#pragma once
#include <stdint.h>

// One cooperative owner; a lease is isolation, not authentication.
class BootloaderLease {
  bool valid = false, claimed = false;
  uint8_t target = 0, source = 0;
  uint32_t ip = 0, touched = 0;
  uint16_t port = 0;
public:
  bool active(uint32_t now) const { return valid && uint32_t(now - touched) < 10000; }
  bool targetIs(uint8_t address, uint32_t now) const { return active(now) && target == address; }
  bool owned(uint32_t now) const { return active(now) && claimed; }
  bool matchesTarget(uint8_t address, uint32_t now) const { return active(now) && (address == 0 || address == target); }
  bool allows(uint8_t dst, uint8_t src, uint32_t remoteIP, uint16_t remotePort, uint32_t now) const {
    return !active(now) || (dst == target && (!claimed || (src == source && remoteIP == ip && remotePort == port)));
  }
  bool claim(uint8_t dst, uint8_t src, uint32_t remoteIP, uint16_t remotePort, uint32_t now) {
    if (!allows(dst, src, remoteIP, remotePort, now)) return false;
    valid = claimed = true; target = dst; source = src; ip = remoteIP; port = remotePort; touched = now;
    return true;
  }
  void observe(uint8_t src, bool start, uint32_t now) {
    if (active(now)) { if (src == target) touched = now; return; }
    if (start) { valid = true; claimed = false; target = src; touched = now; }
  }
};
