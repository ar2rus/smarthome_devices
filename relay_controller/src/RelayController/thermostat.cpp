#include "Thermostat.h"

#include <time.h>
#include <math.h>

// Конструктор с передачей настроек
Thermostat::Thermostat(const ThermostatSettings& _settings, 
                                              std::function<float()> _requestTemperature, 
                                              std::function<void(bool)> _relayControl)
  : requestTemperature(_requestTemperature),
    relayControl(_relayControl),
    stateChangedCallback(nullptr),
    lastReportedState{false, false, 0.0f, 0.0f},
    lastReportedStateValid(false),
    relayState(false),
    currentTemperature(0.0f),
    desiredTemperature(0.0f) {
  
  // Копируем расписание из settings в локальные поля
  scheduleSize = _settings.schedule.size();
  schedule = new ThermostatSchedule[scheduleSize];
  for (int i = 0; i < scheduleSize; i++) {
    schedule[i] = _settings.schedule[i];
  }
  on = _settings.enabled;
}

// Деструктор для освобождения памяти
Thermostat::~Thermostat() {
  if (schedule != nullptr) {
    delete[] schedule;
  }
}

float Thermostat::getDesiredTemperature(int hour, int minute, int dayOfWeek) {
  for (int i = scheduleSize - 1; i >= 0; i--) {
    if (schedule[i].checkDayOfWeek(dayOfWeek)) {
      if (hour > schedule[i].hour || (hour == schedule[i].hour && minute >= schedule[i].minute)) {
        return schedule[i].temperature;
      }
    }
  }

  return 0.0;
}

void Thermostat::handle() {
  time_t t;
  time(&t);
  struct tm* lt = localtime(&t);
  
  desiredTemperature = on ? getDesiredTemperature(lt->tm_hour, lt->tm_min, lt->tm_wday) : 0.0f;
  currentTemperature = getCurrentTemperature();

  if (!on) {
    setRelay(false);
    notifyStateChangedIfNeeded();
    return;
  }
  
  if (desiredTemperature == 0.0f || currentTemperature == 0.0f) {
    setRelay(false);
    notifyStateChangedIfNeeded();
    return;
  }

  if (currentTemperature <= desiredTemperature - THERMOSTAT_TEMPERATURE_HYSTERESIS) {
    setRelay(true);
  } else if (currentTemperature >= desiredTemperature + THERMOSTAT_TEMPERATURE_HYSTERESIS) {
    setRelay(false);
  }

  notifyStateChangedIfNeeded();
}

void Thermostat::setOn(bool enabled) {
  on = enabled;
  handle();  // Вызов handle при изменении состояния системы
}

bool Thermostat::isOn() const {
  return on;
}

void Thermostat::setRelay(bool newState) {
  if (relayState != newState) {
    relayState = newState;
    relayControl(newState);
  }
}

float Thermostat::getCurrentTemperature() {
  float t = requestTemperature();
  if (t < THERMOSTAT_MIN_TEMPERATURE || t > THERMOSTAT_MAX_TEMPERATURE) {
    return 0.0;
  }
  return t;
}

void Thermostat::fillState(ThermostatState* _state) const {
  _state->on = on;
  _state->relayState = on ? relayState : false;
  _state->currentTemperature = on ? currentTemperature : 0.0f;
  _state->desiredTemperature = on ? desiredTemperature : 0.0f;
}

bool Thermostat::isSameState(const ThermostatState& a, const ThermostatState& b) const {
  static const float STATE_EPS = 0.01f;
  return
    a.on == b.on &&
    a.relayState == b.relayState &&
    fabsf(a.currentTemperature - b.currentTemperature) < STATE_EPS &&
    fabsf(a.desiredTemperature - b.desiredTemperature) < STATE_EPS;
}

void Thermostat::notifyStateChangedIfNeeded() {
  ThermostatState currentState;
  fillState(&currentState);

  if (!lastReportedStateValid || !isSameState(currentState, lastReportedState)) {
    lastReportedState = currentState;
    lastReportedStateValid = true;
    if (stateChangedCallback) {
      stateChangedCallback(currentState);
    }
  }
}

void Thermostat::getState(ThermostatState* _state) {
  fillState(_state);
}

void Thermostat::setStateChangedCallback(std::function<void(const ThermostatState&)> callback) {
  stateChangedCallback = callback;
  notifyStateChangedIfNeeded();
}

// Применение настроек из структуры ThermostatSettings (для загрузки из ПЗУ)
void Thermostat::applySettings(const ThermostatSettings& _settings) {
  // Освобождаем старое расписание
  delete[] schedule;
  
  // Копируем расписание из settings в локальные поля
  scheduleSize = _settings.schedule.size();
  schedule = new ThermostatSchedule[scheduleSize];
  for (int i = 0; i < scheduleSize; i++) {
    schedule[i] = _settings.schedule[i];
  }
  on = _settings.enabled;
  handle();
}

// Получение текущих настроек в виде ThermostatSettings (для сохранения в ПЗУ)
ThermostatSettings Thermostat::getSettings() const {
  // Создаем новый объект настроек
  ThermostatSettings settings;
  
  // Заполняем расписание
  settings.schedule.clear();
  for (int i = 0; i < scheduleSize; i++) {
    settings.schedule.push_back(schedule[i]);
  }
  
  // Заполняем состояние включения
  settings.enabled = on;
  
  return settings;
}
