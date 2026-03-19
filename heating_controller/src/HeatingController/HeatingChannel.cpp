#include "HeatingChannel.h"

#include <math.h>

void FilteredTemperature::init(float initialOffset) {
  raw = 0.0f;
  offset = initialOffset;
  filtered = 0.0f;
  sourceValid = false;
  hasFiltered = false;
  updatedAtMs = 0;
  lastFilterMs = 0;
}

void FilteredTemperature::ingest(float value, bool valid, unsigned long sourceUpdatedAtMs) {
  sourceValid = valid;
  updatedAtMs = sourceUpdatedAtMs;
  if (valid) {
    raw = value;
  }
}

void FilteredTemperature::applyEma(float tauSec, unsigned long nowMs) {
  if (!sourceValid) {
    return;
  }

  float corrected = raw + offset;
  if (!hasFiltered) {
    filtered = corrected;
    hasFiltered = true;
    lastFilterMs = nowMs;
    return;
  }

  unsigned long dtMs = nowMs - lastFilterMs;
  if (dtMs == 0) {
    return;
  }

  float dtSec = dtMs / 1000.0f;
  if (dtSec <= 0.0f) {
    return;
  }

  float alpha = dtSec / (tauSec + dtSec);
  if (alpha < 0.0f) alpha = 0.0f;
  if (alpha > 1.0f) alpha = 1.0f;

  filtered += alpha * (corrected - filtered);
  lastFilterMs = nowMs;
}

bool FilteredTemperature::isActual(unsigned long nowMs, unsigned long maxAgeMs) const {
  if (!sourceValid || !hasFiltered) {
    return false;
  }
  return (nowMs - updatedAtMs) <= maxAgeMs;
}

HeatingChannel::HeatingChannel() :
  channelIndex_(0),
  temperatureLookup_(nullptr),
  relayControl_(nullptr),
  sourceMaxAgeMs_(0),
  periodStartMs_(0),
  lastPollMs_(0),
  hasAppliedRelay_(false),
  lastAppliedRelayOn_(false),
  controlTonSec_(0),
  thermostatDemand_(false),
  lastTref_(0.0f),
  hasLastTref_(false),
  prevRet_(0.0f),
  prevDelta_(0.0f),
  hasPrevWater_(false),
  stagnantCycles_(0) {
  state_.relayOn = false;
  state_.relayStateChangedAtMs = 0;
  state_.tonSec = 0;
  state_.hasRoomTemp = false;
  state_.roomTemp = 0.0f;
  state_.roomSensorCount = 0;
  for (uint8_t i = 0; i < MAX_ROOM_GROUP_SIZE; i++) {
    state_.hasRoomSensorTemp[i] = false;
    state_.roomSensorTemp[i] = 0.0f;
    state_.hasRoomSensorRawTemp[i] = false;
    state_.roomSensorRawTemp[i] = 0.0f;
  }
  state_.hasSupplyTemp = false;
  state_.supplyTemp = 0.0f;
  state_.hasReturnTemp = false;
  state_.returnTemp = 0.0f;
  state_.faultWarning = false;
}

void HeatingChannel::configure(
  uint8_t channelIndex,
  const HeatingChannelConfig& config,
  TemperatureLookupCallback temperatureLookup,
  RelayControlCallback relayControl,
  unsigned long sourceMaxAgeMs
) {
  channelIndex_ = channelIndex;
  config_ = config;
  temperatureLookup_ = temperatureLookup;
  relayControl_ = relayControl;
  sourceMaxAgeMs_ = sourceMaxAgeMs;

  supplyTemperature_.init(config_.offsetSupply);
  returnTemperature_.init(config_.offsetReturn);
  for (uint8_t i = 0; i < MAX_ROOM_GROUP_SIZE; i++) {
    roomTemperatures_[i].init(config_.offsetRoom[i]);
  }
}

void HeatingChannel::begin(unsigned long nowMs) {
  periodStartMs_ = nowMs;
  lastPollMs_ = 0;
  hasAppliedRelay_ = false;
  hasLastTref_ = false;
  hasPrevWater_ = false;
  stagnantCycles_ = 0;
  state_.relayStateChangedAtMs = nowMs;
  controlTonSec_ = state_.tonSec;
  thermostatDemand_ = false;

  pollInputs(nowMs);
  updateForNewPeriod(nowMs);
}

void HeatingChannel::setMode(ChannelControlMode mode) {
  config_.mode = mode;
}

void HeatingChannel::setManualPercent(float percent) {
  config_.manualPercent = percent;
}

ChannelControlMode HeatingChannel::mode() const {
  return config_.mode;
}

const HeatingChannelState& HeatingChannel::state() const {
  return state_;
}

const HeatingChannelConfig& HeatingChannel::config() const {
  return config_;
}

uint16_t HeatingChannel::clampTon(uint16_t value) const {
  if (value < config_.tonMinSec) {
    value = config_.tonMinSec;
  }
  if (value > config_.tonMaxSec) {
    value = config_.tonMaxSec;
  }
  return value;
}

uint16_t HeatingChannel::percentToTon(float percent) const {
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;
  float mapped = config_.tonMinSec + (config_.tonMaxSec - config_.tonMinSec) * (percent / 100.0f);
  return static_cast<uint16_t>(mapped + 0.5f);
}

void HeatingChannel::pollInputs(unsigned long nowMs) {
  if (lastPollMs_ != 0 && (nowMs - lastPollMs_) < 1000UL) {
    return;
  }
  lastPollMs_ = nowMs;

  auto ingestById = [&](const char* sensorId, FilteredTemperature* sensor, float tauSec) {
    if (sensor == nullptr) {
      return;
    }

    float value = 0.0f;
    unsigned long updatedAtMs = nowMs;

    bool found = false;
    if (sensorId != nullptr && temperatureLookup_ != nullptr) {
      found = temperatureLookup_(sensorId, &value, &updatedAtMs);
    }

    bool actual = false;
    if (found && (nowMs - updatedAtMs) <= sourceMaxAgeMs_) {
      actual = true;
    }

    sensor->ingest(value, actual, updatedAtMs);
    sensor->applyEma(tauSec, nowMs);
  };

  ingestById(config_.supplySensorId, &supplyTemperature_, config_.tauSupplySec);
  ingestById(config_.returnSensorId, &returnTemperature_, config_.tauReturnSec);

  for (uint8_t i = 0; i < config_.roomSensorCount && i < MAX_ROOM_GROUP_SIZE; i++) {
    ingestById(config_.roomSensorIds[i], &roomTemperatures_[i], config_.tauRoomSec);
  }

  state_.roomSensorCount = (config_.roomSensorCount <= MAX_ROOM_GROUP_SIZE) ? config_.roomSensorCount : MAX_ROOM_GROUP_SIZE;
  for (uint8_t i = 0; i < MAX_ROOM_GROUP_SIZE; i++) {
    if (i < state_.roomSensorCount) {
      bool actual = roomTemperatures_[i].isActual(nowMs, sourceMaxAgeMs_);
      state_.hasRoomSensorTemp[i] = actual;
      state_.hasRoomSensorRawTemp[i] = actual;
      if (actual) {
        state_.roomSensorTemp[i] = roomTemperatures_[i].filtered;
        state_.roomSensorRawTemp[i] = roomTemperatures_[i].raw;
      }
    } else {
      state_.hasRoomSensorTemp[i] = false;
      state_.roomSensorTemp[i] = 0.0f;
      state_.hasRoomSensorRawTemp[i] = false;
      state_.roomSensorRawTemp[i] = 0.0f;
    }
  }

  float tref = 0.0f;
  state_.hasRoomTemp = computeTref(nowMs, &tref);
  if (state_.hasRoomTemp) {
    state_.roomTemp = tref;
  }

  state_.hasSupplyTemp = supplyTemperature_.isActual(nowMs, sourceMaxAgeMs_);
  if (state_.hasSupplyTemp) {
    state_.supplyTemp = supplyTemperature_.filtered;
  }

  state_.hasReturnTemp = returnTemperature_.isActual(nowMs, sourceMaxAgeMs_);
  if (state_.hasReturnTemp) {
    state_.returnTemp = returnTemperature_.filtered;
  }
}

bool HeatingChannel::computeTref(unsigned long nowMs, float* outTref) const {
  if (outTref == nullptr) {
    return false;
  }

  float values[MAX_ROOM_GROUP_SIZE];
  uint8_t valueCount = 0;
  for (uint8_t i = 0; i < config_.roomSensorCount && i < MAX_ROOM_GROUP_SIZE; i++) {
    if (roomTemperatures_[i].isActual(nowMs, sourceMaxAgeMs_)) {
      values[valueCount++] = roomTemperatures_[i].filtered;
    }
  }

  if (valueCount == 0) {
    return false;
  }

  if (config_.roomTemperatureStrategy == ROOM_TEMPERATURE_STRATEGY_TARGET) {
    if (config_.targetRoomIndex < valueCount &&
        roomTemperatures_[config_.targetRoomIndex].isActual(nowMs, sourceMaxAgeMs_)) {
      *outTref = roomTemperatures_[config_.targetRoomIndex].filtered;
      return true;
    }
    *outTref = values[0];
    return true;
  }

  if (config_.roomTemperatureStrategy == ROOM_TEMPERATURE_STRATEGY_MIN) {
    float minValue = values[0];
    for (uint8_t i = 1; i < valueCount; i++) {
      if (values[i] < minValue) {
        minValue = values[i];
      }
    }
    *outTref = minValue;
    return true;
  }

  for (uint8_t i = 0; i < valueCount; i++) {
    for (uint8_t j = i + 1; j < valueCount; j++) {
      if (values[j] < values[i]) {
        float tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
      }
    }
  }
  *outTref = values[valueCount / 2];
  return true;
}

void HeatingChannel::updateForNewPeriod(unsigned long nowMs) {
  if (config_.mode == CHANNEL_CONTROL_MODE_AUTO) {
    // Classic thermostat mode is updated each second in handle(), not once per PWM period.
    return;
  }

  uint16_t previousTon = controlTonSec_;
  uint16_t newTon = previousTon;
  bool wantedIncrease = false;
  float tref = 0.0f;
  bool hasTref = computeTref(nowMs, &tref);

  state_.hasRoomTemp = hasTref;
  if (hasTref) {
    state_.roomTemp = tref;
  }

  if (config_.mode == CHANNEL_CONTROL_MODE_OFF) {
    newTon = 0;
  } else if (config_.mode == CHANNEL_CONTROL_MODE_ON) {
    newTon = config_.pwmPeriodSec;
  } else if (config_.mode == CHANNEL_CONTROL_MODE_PWM_PERCENT) {
    newTon = percentToTon(config_.manualPercent);
  } else {
    if (!hasTref) {
      newTon = config_.tonSafeSec;
    } else {
      float error = config_.tSet - tref;

      if (tref < config_.tSet - config_.hRoom) {
        newTon = previousTon + config_.stepSec;
        wantedIncrease = true;
      } else if (tref > config_.tSet + config_.hRoom) {
        newTon = (previousTon > config_.stepSec) ? (previousTon - config_.stepSec) : 0;
      }

      if (config_.trendGuardEnable && hasLastTref_) {
        float dTref = tref - lastTref_;
        bool closeToTarget = fabsf(error) <= (config_.hRoom * 1.5f);
        if (wantedIncrease && dTref > 0.03f && closeToTarget) {
          newTon = previousTon;
          wantedIncrease = false;
        }
        if (!wantedIncrease && error < 0.0f && dTref < -0.03f) {
          newTon = previousTon;
        }
      }

      lastTref_ = tref;
      hasLastTref_ = true;
    }
  }

  bool applyWaterLimits = (config_.mode == CHANNEL_CONTROL_MODE_AUTO_PWM) ||
    (config_.mode == CHANNEL_CONTROL_MODE_PWM_PERCENT && config_.manualWaterLimitsEnable);

  if (applyWaterLimits && returnTemperature_.isActual(nowMs, sourceMaxAgeMs_)) {
    float tRet = returnTemperature_.filtered;

    if (tRet > config_.tRetMax + config_.hRet) {
      uint16_t stepDown = config_.stepSec * 2;
      newTon = (newTon > stepDown) ? (newTon - stepDown) : 0;
      if (newTon < config_.tonMinSec) {
        newTon = config_.tonMinSec;
      }
      wantedIncrease = false;
    } else if (tRet > config_.tRetMax) {
      newTon = (newTon > config_.stepSec) ? (newTon - config_.stepSec) : 0;
      if (newTon < config_.tonMinSec) {
        newTon = config_.tonMinSec;
      }
      wantedIncrease = false;
    }

    if (wantedIncrease && supplyTemperature_.isActual(nowMs, sourceMaxAgeMs_)) {
      float deltaT = supplyTemperature_.filtered - tRet;
      if (deltaT < config_.deltaTMin) {
        newTon = previousTon;
        wantedIncrease = false;
      }
    }
  }

  if (config_.mode == CHANNEL_CONTROL_MODE_OFF) {
    state_.tonSec = 0;
  } else if (config_.mode == CHANNEL_CONTROL_MODE_ON) {
    controlTonSec_ = config_.pwmPeriodSec;
    state_.tonSec = config_.pwmPeriodSec;
  } else {
    controlTonSec_ = clampTon(newTon);
    if (
      config_.mode == CHANNEL_CONTROL_MODE_AUTO_PWM &&
      wantedIncrease &&
      config_.tonMinSec > 0 &&
      controlTonSec_ <= config_.tonMinSec &&
      config_.tonMinSec < config_.tonMaxSec
    ) {
      controlTonSec_ = clampTon(static_cast<uint16_t>(config_.tonMinSec + config_.stepSec));
    }
    state_.tonSec = controlTonSec_;
  }

  state_.faultWarning = false;
  if (returnTemperature_.isActual(nowMs, sourceMaxAgeMs_)) {
    float tRet = returnTemperature_.filtered;
    bool deltaAvailable = supplyTemperature_.isActual(nowMs, sourceMaxAgeMs_);
    float deltaT = deltaAvailable ? (supplyTemperature_.filtered - tRet) : 0.0f;
    bool highTon = state_.tonSec > static_cast<uint16_t>(config_.pwmPeriodSec * 0.7f);

    if (highTon) {
      bool almostNoChange = false;
      if (hasPrevWater_) {
        float dRet = fabsf(tRet - prevRet_);
        float dDelta = deltaAvailable ? fabsf(deltaT - prevDelta_) : 0.0f;
        almostNoChange = (dRet < 0.2f) && (!deltaAvailable || dDelta < 0.2f);
      }

      if (almostNoChange) {
        if (stagnantCycles_ < 255) {
          stagnantCycles_++;
        }
      } else {
        stagnantCycles_ = 0;
      }
    } else {
      stagnantCycles_ = 0;
    }

    prevRet_ = tRet;
    prevDelta_ = deltaT;
    hasPrevWater_ = true;
    state_.faultWarning = stagnantCycles_ >= 3;
  }
}

bool HeatingChannel::isMinPulseSuppressed() const {
  if (config_.mode == CHANNEL_CONTROL_MODE_OFF || config_.mode == CHANNEL_CONTROL_MODE_ON) {
    return false;
  }
  if (config_.mode == CHANNEL_CONTROL_MODE_AUTO) {
    return false;
  }
  return config_.tonMinSec > 0 && state_.tonSec > 0 && state_.tonSec <= config_.tonMinSec;
}

void HeatingChannel::handle(unsigned long nowMs) {
  pollInputs(nowMs);

  unsigned long periodMs = static_cast<unsigned long>(config_.pwmPeriodSec) * 1000UL;
  if (periodMs == 0) {
    periodMs = 1000UL;
  }

  while (nowMs - periodStartMs_ >= periodMs) {
    periodStartMs_ += periodMs;
    updateForNewPeriod(nowMs);
  }

  if (config_.mode == CHANNEL_CONTROL_MODE_AUTO) {
    float tref = 0.0f;
    bool hasTref = computeTref(nowMs, &tref);
    state_.hasRoomTemp = hasTref;
    if (hasTref) {
      state_.roomTemp = tref;
      if (!thermostatDemand_ && tref < (config_.tSet - config_.hRoom)) {
        thermostatDemand_ = true;
      } else if (thermostatDemand_ && tref > (config_.tSet + config_.hRoom)) {
        thermostatDemand_ = false;
      }
    } else {
      thermostatDemand_ = false;
    }
    state_.tonSec = thermostatDemand_ ? config_.pwmPeriodSec : 0;
    controlTonSec_ = state_.tonSec;
  }

  unsigned long elapsedMs = nowMs - periodStartMs_;
  unsigned long onMs = static_cast<unsigned long>(state_.tonSec) * 1000UL;
  bool computedRelayOn = false;
  if (config_.mode == CHANNEL_CONTROL_MODE_ON) {
    computedRelayOn = true;
  } else if (config_.mode == CHANNEL_CONTROL_MODE_AUTO) {
    computedRelayOn = thermostatDemand_;
  } else {
    computedRelayOn = !isMinPulseSuppressed() && (state_.tonSec > 0) && (elapsedMs < onMs);
  }
  if (!hasAppliedRelay_ || lastAppliedRelayOn_ != computedRelayOn) {
    state_.relayStateChangedAtMs = nowMs;
  }
  state_.relayOn = computedRelayOn;

  if (relayControl_ != nullptr) {
    if (!hasAppliedRelay_ || lastAppliedRelayOn_ != state_.relayOn) {
      relayControl_(channelIndex_, state_.relayOn);
      lastAppliedRelayOn_ = state_.relayOn;
      hasAppliedRelay_ = true;
    }
  }
}
