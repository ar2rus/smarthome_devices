#pragma once
#include <Arduino.h>
#include <functional>
#include <math.h>

class HumidityAutoController {
public:
  struct Params {
    float emaAlpha = 0.05;
    float baselineAlpha = 0.001;
    float triggerDelta = 6.0;
    float triggerRate = 0.3;
    unsigned long confirmMs = 5000;
    unsigned long cooldownMs = 1800000;
    unsigned long windowMs = 10000;
    int bufferSize = 100;
  };

  using TriggerCallback = std::function<void(void)>;

  enum class State {
    IDLE,
    HUMIDITY_RISING,
    TRIGGERED
  };

  HumidityAutoController(const Params& params, TriggerCallback cb)
    : params_(params), onTrigger_(cb)
  {
    buffer_ = new Reading[params_.bufferSize];
  }

  ~HumidityAutoController() {
    delete[] buffer_;
  }

  void update(float humidity, unsigned long nowMs) {
    filter(humidity);
    initBaseline();
    addReading(filtered_, nowMs);
    updateBaseline();
    updateStateMachine(nowMs);
  }

  // ===== getters =====
  float raw() const { return lastRaw_; }
  float filtered() const { return filtered_; }
  float baseline() const { return baseline_; }
  float delta() const { return filtered_ - baseline_; }

  float growthRate(unsigned long nowMs) const {
    return calcGrowthRate(nowMs);
  }

  unsigned long confirmTime(unsigned long nowMs) const {
    if (state_ != State::HUMIDITY_RISING) return 0;
    return nowMs - riseConfirmStart_;
  }

  unsigned long cooldownLeft(unsigned long nowMs) const {
    if (!hasTriggeredOnce_) return 0;
    if (state_ != State::TRIGGERED) return 0;
    if (nowMs >= lastTrigger_ + params_.cooldownMs) return 0;
    return (lastTrigger_ + params_.cooldownMs) - nowMs;
  }

  State state() const { return state_; }

  const char* stateString() const {
    switch (state_) {
      case State::IDLE: return "IDLE";
      case State::HUMIDITY_RISING: return "HUMIDITY_RISING";
      case State::TRIGGERED: return "TRIGGERED";
      default: return "UNKNOWN";
    }
  }

private:
  struct Reading {
    float value;
    unsigned long ts;
  };

  Params params_;
  TriggerCallback onTrigger_;

  Reading* buffer_;
  int bufIndex_ = 0;
  bool bufFilled_ = false;

  float lastRaw_ = NAN;
  float filtered_ = NAN;
  float baseline_ = NAN;

  State state_ = State::IDLE;

  unsigned long riseConfirmStart_ = 0;
  unsigned long lastTrigger_ = 0;
  bool hasTriggeredOnce_ = false;

  // ---------- internals ----------

  void filter(float h) {
    lastRaw_ = h;
    if (isnan(filtered_)) filtered_ = h;
    else filtered_ = params_.emaAlpha * h + (1 - params_.emaAlpha) * filtered_;
  }

  void initBaseline() {
    if (isnan(baseline_)) baseline_ = filtered_;
  }

  void updateBaseline() {
    float d = filtered_ - baseline_;
    if (abs(d) < 3.0) {
      baseline_ += params_.baselineAlpha * (filtered_ - baseline_);
    }
  }

  void addReading(float v, unsigned long ts) {
    buffer_[bufIndex_++] = {v, ts};
    if (bufIndex_ >= params_.bufferSize) {
      bufIndex_ = 0;
      bufFilled_ = true;
    }
  }

  float calcGrowthRate(unsigned long now) const {
    unsigned long start = now - params_.windowMs;
    bool found = false;
    Reading oldest{}, newest{};

    int n = bufFilled_ ? params_.bufferSize : bufIndex_;
    for (int i = 0; i < n; i++) {
      const Reading& r = buffer_[i];
      if (r.ts >= start && r.ts <= now) {
        if (!found || r.ts < oldest.ts) oldest = r;
        if (!found || r.ts > newest.ts) newest = r;
        found = true;
      }
    }

    if (!found || newest.ts == oldest.ts) return 0;
    float dt = (newest.ts - oldest.ts) / 1000.0;
    return (newest.value - oldest.value) / dt;
  }

  void updateStateMachine(unsigned long now) {
    float d = filtered_ - baseline_;
    float rate = calcGrowthRate(now);

    switch (state_) {

      case State::IDLE:
        if (d >= params_.triggerDelta && rate >= params_.triggerRate) {
          riseConfirmStart_ = now;
          state_ = State::HUMIDITY_RISING;
        }
        break;

      case State::HUMIDITY_RISING:
        if (!(d >= params_.triggerDelta && rate >= params_.triggerRate)) {
          state_ = State::IDLE;
          riseConfirmStart_ = 0;
        } else if (now - riseConfirmStart_ >= params_.confirmMs) {
          onTrigger_();
          lastTrigger_ = now;
          hasTriggeredOnce_ = true;
          state_ = State::TRIGGERED;
        }
        break;

      case State::TRIGGERED:
        if (now >= lastTrigger_ + params_.cooldownMs) {
          state_ = State::IDLE;
        }
        break;
    }
  }
};
