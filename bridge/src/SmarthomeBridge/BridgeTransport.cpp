#include <Arduino.h>
#include "BridgeTransport.h"
extern uint8_t uart_send_message(char code, char* data, uint8_t length);
namespace BridgeTransport {
static Diagnostics stats;
static Owner owner = NONE;
static Result result = NO_RESULT;
static uint16_t id = 0, nextId = 0, budget = 0, remoteId = 0;
static uint32_t started = 0, queried = 0;
static uint8_t nonce = 0, destination = 0, command = 0;
static bool queryPending = false, remoteReady = false, countedAccepted = false, paused = false;
static uint16_t read16(const char* p) { return uint8_t(p[0]) | (uint16_t(uint8_t(p[1])) << 8); }
const Diagnostics& diagnostics() { return stats; }
bool busy() { return owner != NONE; }
bool ready() { return !paused && !busy() && stats.protocol == 2 && remoteReady && millis() - stats.lastReplyAt < 500; }
static void finish(Result value) {
  if (result != PENDING) return;
  if ((value == TRANSMITTED || value == EXPIRED) && !countedAccepted) { ++stats.accepted; countedAccepted = true; }
  result = value; stats.lastId = id; stats.lastResult = value;
  if (value == TRANSMITTED) ++stats.transmitted;
  else if (value == EXPIRED) ++stats.expired;
  else if (value == REJECTED) ++stats.rejected;
  else if (value == UNKNOWN) ++stats.unknown;
}
void receive(const char* data, size_t length) {
  if (!data || length < 3 || !queryPending || uint8_t(data[0]) != nonce) return;
  queryPending = false; stats.protocol = uint8_t(data[1]); remoteReady = false;
  if (length != 18 || stats.protocol != 2) return;
  stats.lastReplyAt = millis(); remoteReady = data[2] == 1;
  remoteId = read16(data + 3);
  stats.avrHardwareErrors = read16(data + 6); stats.avrQueueDrops = read16(data + 8); stats.avrOverflows = read16(data + 10);
  stats.avrLegacyExpired = read16(data + 12); stats.avrCrcErrors = read16(data + 14); stats.avrStackMinFree = read16(data + 16);
  if (!nextId) { nextId = remoteId + 1; if (!nextId) nextId = 1; }
  if (owner != NONE && result == PENDING) {
    if (remoteId == id) {
      uint8_t state = data[5];
      if (state == PENDING && !countedAccepted) { ++stats.accepted; countedAccepted = true; }
      if (state >= TRANSMITTED && state <= REJECTED) finish(static_cast<Result>(state));
    } else if (remoteReady && millis() - started >= 500) finish(UNKNOWN);
  }
}
bool start(Owner requestedOwner, const char* packet, uint8_t length, uint16_t ttl) {
  if (!ready() || !packet || length < 4 || length > 72 || !ttl || ttl > 2000 || requestedOwner == NONE) return false;
  char frame[76];
  uint16_t candidate = nextId;
  if (!candidate || candidate == remoteId) { candidate = remoteId + 1; if (!candidate) candidate = 1; }
  frame[0] = candidate; frame[1] = candidate >> 8; frame[2] = ttl; frame[3] = ttl >> 8;
  memcpy(frame + 4, packet, length);
  if (!uart_send_message(5, frame, length + 4)) return false;
  destination = uint8_t(packet[1]); command = uint8_t(packet[2]);
  id = candidate; nextId = id + 1; if (!nextId) nextId = 1;
  owner = requestedOwner; result = PENDING; budget = ttl; started = millis(); countedAccepted = false; remoteReady = false;
  return true;
}
void legacyBootStarted() {
  if (owner == NORMAL && destination == 0 && command == 3) finish(TRANSMITTED);
}
void cancel() {
  if (owner == NONE || result != PENDING || paused) return;
  char frame[2] = {char(id), char(id >> 8)};
  uart_send_message(7, frame, sizeof(frame)); // Idempotent cancellation, never a command retry.
}
Result take(Owner requestedOwner) {
  if (owner != requestedOwner || result == PENDING) return NO_RESULT;
  Result value = result; owner = NONE; result = NO_RESULT;
  return value;
}
void process(bool pause, bool urgent) {
  paused = pause;
  const uint32_t now = millis();
  if (owner != NONE && result == PENDING && now - started >= uint32_t(budget) + 1000) finish(UNKNOWN);
  if (paused) { remoteReady = false; queryPending = false; return; }
  uint32_t interval = (urgent || busy()) ? 20 : 1000;
  if (now - queried >= (queryPending ? 200u : interval)) {
    char value = nonce + 1;
    if (uart_send_message(3, &value, 1)) { nonce = uint8_t(value); queried = now; queryPending = true; }
  }
}
}
