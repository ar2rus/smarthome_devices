#pragma once
#include <stdint.h>
#include <stddef.h>
// Host-side checks only. No bytes are added to the legacy bootloader protocol.
class LegacyFlashFlow {
  enum State : uint8_t { START, WAIT_READY, READY, WAIT_WRITE, DONE, FAILED } state = START;
  uint16_t pageSize = 0, offset = 0, pending = 0;
  uint8_t page = 0;
  uint32_t submittedAt = 0;
public:
  static bool validPageSize(uint16_t size) { return size >= 32 && size <= 256 && !(size & (size - 1)); }
  bool allows(const char* data, size_t length, uint32_t now) const {
    if (!data || !length || state == DONE || state == FAILED) return false;
    if ((state == WAIT_READY || state == WAIT_WRITE) && now - submittedAt >= 5000) return false;
    const uint8_t op = uint8_t(data[0]);
    if (op == 1) return state == START && length == 1;
    if (op == 5) return state == READY && length == 1;
    if (op != 3 || state != READY || length < 5 || uint8_t(data[1]) != page) return false;
    uint16_t size = uint8_t(data[2]) | (uint16_t(uint8_t(data[3])) << 8);
    return size && !(size & 1) && size <= 64 && length == size + 4u && offset + size <= pageSize &&
           uint32_t(page) * pageSize + offset + size <= 7168 && page < 128;
  }
  void submitted(const char* data, size_t length, uint32_t now) {
    if (!allows(data, length, now)) { state = FAILED; return; }
    submittedAt = now;
    if (data[0] == 1) state = WAIT_READY;
    else if (data[0] == 3) { pending = uint8_t(data[2]) | (uint16_t(uint8_t(data[3])) << 8); state = WAIT_WRITE; }
    else state = DONE;
  }
  void response(const char* data, size_t length) {
    if (!data || !length) return;
    if (uint8_t(data[0]) == 255) { state = FAILED; return; }
    if (state == WAIT_READY && data[0] == 2 && length == 3) {
      uint16_t size = uint8_t(data[1]) | (uint16_t(uint8_t(data[2])) << 8);
      if (!validPageSize(size)) { state = FAILED; return; }
      pageSize = size; state = READY;
    } else if (state == WAIT_WRITE && data[0] == 4 && length == 1) {
      offset += pending; pending = 0;
      if (offset == pageSize) { ++page; offset = 0; }
      state = READY;
    }
  }
  void fail() { state = FAILED; }
};
