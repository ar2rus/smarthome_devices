#pragma once
#include <stdint.h>
#include <stddef.h>
namespace BridgeTransport {
enum Owner : uint8_t { NONE, NORMAL, FLASH, PROBE };
enum Result : uint8_t { NO_RESULT, PENDING, TRANSMITTED, EXPIRED, REJECTED, UNKNOWN };
struct Diagnostics {
  uint32_t accepted = 0, transmitted = 0, expired = 0, rejected = 0, unknown = 0;
  uint32_t lastReplyAt = 0;
  uint16_t lastId = 0, avrHardwareErrors = 0, avrQueueDrops = 0, avrOverflows = 0;
  uint16_t avrLegacyExpired = 0, avrCrcErrors = 0, avrStackMinFree = 0;
  uint8_t lastResult = 0, protocol = 0;
};
void process(bool paused, bool urgent);
void receive(const char* data, size_t length);
bool ready();
bool busy();
bool start(Owner owner, const char* packet, uint8_t length, uint16_t budget);
void cancel();
void legacyBootStarted();
Result take(Owner owner);
const Diagnostics& diagnostics();
}
