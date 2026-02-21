#ifndef ONE_WIRE_WATCHDOG_H
#define ONE_WIRE_WATCHDOG_H

#include <functional>

class OneWireWatchdog {
  private:
    const unsigned long gracePeriodMs;
    const unsigned long restartOffDelayMs;
    std::function<void(bool)> restartPower;

    bool monitoringEnabled;
    bool restartInProgress;
    unsigned long restartStartedMs;
    unsigned long checkBlockedUntilMs;

    bool isTimeReached(unsigned long nowMs, unsigned long targetMs) const;

  public:
    OneWireWatchdog(
      unsigned long gracePeriodMs,
      unsigned long restartOffDelayMs,
      std::function<void(bool)> restartPower
    );

    void start(unsigned long nowMs);
    void stop();
    void handle(unsigned long nowMs, bool alive);
};

#endif
