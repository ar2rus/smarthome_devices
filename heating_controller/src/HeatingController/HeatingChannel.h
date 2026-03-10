#ifndef HEATING_CHANNEL_H
#define HEATING_CHANNEL_H

#include <Arduino.h>

static const uint8_t MAX_ROOM_GROUP_SIZE = 4;

enum RoomTemperatureStrategy : uint8_t {
  ROOM_TEMPERATURE_STRATEGY_MIN = 0,
  ROOM_TEMPERATURE_STRATEGY_TARGET = 1,
  ROOM_TEMPERATURE_STRATEGY_MEDIAN = 2
};

enum ChannelControlMode : uint8_t {
  CHANNEL_CONTROL_MODE_AUTO_PWM = 0,
  CHANNEL_CONTROL_MODE_PWM_PERCENT = 1,
  CHANNEL_CONTROL_MODE_OFF = 2,
  CHANNEL_CONTROL_MODE_ON = 3,
  CHANNEL_CONTROL_MODE_AUTO = 4
};

struct HeatingChannelConfig {
  ChannelControlMode mode;
  float tSet;
  float hRoom;
  uint16_t pwmPeriodSec;
  uint16_t tonMinSec;
  uint16_t tonMaxSec;
  uint16_t stepSec;
  RoomTemperatureStrategy roomTemperatureStrategy;
  uint8_t targetRoomIndex;

  const char* supplySensorId;
  const char* returnSensorId;
  const char* roomSensorIds[MAX_ROOM_GROUP_SIZE];
  uint8_t roomSensorCount;

  float offsetSupply;
  float offsetReturn;
  float offsetRoom[MAX_ROOM_GROUP_SIZE];

  float tauRoomSec;
  float tauSupplySec;
  float tauReturnSec;

  float tRetMax;
  float hRet;
  float deltaTMin;

  float manualPercent;
  bool manualWaterLimitsEnable;

  uint16_t tonSafeSec;
  bool trendGuardEnable;
  char channelName[32];
};

static const HeatingChannelConfig DEFAULT_HEATING_CHANNEL_CONFIG = {
  CHANNEL_CONTROL_MODE_AUTO,
  21.5f,
  0.2f,
  480,
  90,
  420,
  30,
  ROOM_TEMPERATURE_STRATEGY_MIN,
  0,
  nullptr,
  nullptr,
  {nullptr, nullptr, nullptr, nullptr},
  0,
  0.0f,
  0.0f,
  {0.0f, 0.0f, 0.0f, 0.0f},
  900.0f,
  180.0f,
  180.0f,
  45.0f,
  1.5f,
  4.0f,
  50.0f,
  true,
  120,
  true,
  ""
};

struct HeatingChannelState {
  bool relayOn;
  unsigned long relayStateChangedAtMs;
  uint16_t tonSec;
  bool hasRoomTemp;
  float roomTemp;
  uint8_t roomSensorCount;
  bool hasRoomSensorTemp[MAX_ROOM_GROUP_SIZE];
  float roomSensorTemp[MAX_ROOM_GROUP_SIZE];
  bool hasRoomSensorRawTemp[MAX_ROOM_GROUP_SIZE];
  float roomSensorRawTemp[MAX_ROOM_GROUP_SIZE];
  bool hasSupplyTemp;
  float supplyTemp;
  bool hasReturnTemp;
  float returnTemp;
  bool faultWarning;
};

typedef bool (*TemperatureLookupCallback)(
  const char* sensorId,
  float* outTemperature,
  unsigned long* outUpdatedAtMs
);

typedef void (*RelayControlCallback)(uint8_t channelIndex, bool on);

struct FilteredTemperature {
  float raw;
  float offset;
  float filtered;
  bool sourceValid;
  bool hasFiltered;
  unsigned long updatedAtMs;
  unsigned long lastFilterMs;

  void init(float initialOffset = 0.0f);
  void ingest(float value, bool valid, unsigned long updatedAtMs);
  void applyEma(float tauSec, unsigned long nowMs);
  bool isActual(unsigned long nowMs, unsigned long maxAgeMs) const;
};

class HeatingChannel {
public:
  HeatingChannel();

  void configure(
    uint8_t channelIndex,
    const HeatingChannelConfig& config,
    TemperatureLookupCallback temperatureLookup,
    RelayControlCallback relayControl,
    unsigned long sourceMaxAgeMs
  );

  void begin(unsigned long nowMs);
  void handle(unsigned long nowMs);

  void setMode(ChannelControlMode mode);
  void setManualPercent(float percent);
  ChannelControlMode mode() const;

  const HeatingChannelState& state() const;
  const HeatingChannelConfig& config() const;

private:
  uint8_t channelIndex_;
  HeatingChannelConfig config_;

  TemperatureLookupCallback temperatureLookup_;
  RelayControlCallback relayControl_;
  unsigned long sourceMaxAgeMs_;

  FilteredTemperature supplyTemperature_;
  FilteredTemperature returnTemperature_;
  FilteredTemperature roomTemperatures_[MAX_ROOM_GROUP_SIZE];

  unsigned long periodStartMs_;
  unsigned long lastPollMs_;
  bool hasAppliedRelay_;
  bool lastAppliedRelayOn_;
  uint16_t controlTonSec_;
  bool thermostatDemand_;

  float lastTref_;
  bool hasLastTref_;
  float prevRet_;
  float prevDelta_;
  bool hasPrevWater_;
  uint8_t stagnantCycles_;

  HeatingChannelState state_;

  uint16_t clampTon(uint16_t value) const;
  uint16_t percentToTon(float percent) const;

  void pollInputs(unsigned long nowMs);
  bool computeTref(unsigned long nowMs, float* outTref) const;
  void updateForNewPeriod(unsigned long nowMs);
  bool isMinPulseSuppressed() const;
};

#endif
