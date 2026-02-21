#include "OneWireWatchdog.h"

OneWireWatchdog::OneWireWatchdog(
  unsigned long gracePeriodMs,
  unsigned long restartOffDelayMs,
  std::function<void(bool)> restartPower
) : gracePeriodMs(gracePeriodMs),
    restartOffDelayMs(restartOffDelayMs),
    restartPower(restartPower),
    monitoringEnabled(false),
    restartInProgress(false),
    restartStartedMs(0),
    checkBlockedUntilMs(0) {
}

bool OneWireWatchdog::isTimeReached(unsigned long nowMs, unsigned long targetMs) const {
  return static_cast<long>(nowMs - targetMs) >= 0;
}

void OneWireWatchdog::start(unsigned long nowMs) {
  monitoringEnabled = true;
  restartInProgress = false;
  checkBlockedUntilMs = nowMs + gracePeriodMs;
}

void OneWireWatchdog::stop() {
  monitoringEnabled = false;
  restartInProgress = false;
}

void OneWireWatchdog::handle(unsigned long nowMs, bool alive) {
  if (!monitoringEnabled) {
    return;
  }

  if (restartInProgress) {
    if (nowMs - restartStartedMs >= restartOffDelayMs) {
      restartPower(true);
      start(nowMs);
    }
    return;
  }

  if (!isTimeReached(nowMs, checkBlockedUntilMs)) {
    return;
  }

  if (!alive) {
    restartPower(false);
    restartInProgress = true;
    restartStartedMs = nowMs;
  }
}
