#include "thermostat.h"

#include <time.h>
#include <math.h>

// Конструктор с передачей настроек
ThermostatController::ThermostatController(const ThermostatSettings& _settings, 
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
ThermostatController::~ThermostatController() {
  if (schedule != nullptr) {
    delete[] schedule;
  }
}

float ThermostatController::getDesiredTemperature(int hour, int minute, int dayOfWeek) {
  for (int i = scheduleSize - 1; i >= 0; i--) {
    if (schedule[i].checkDayOfWeek(dayOfWeek)) {
      if (hour > schedule[i].hour || (hour == schedule[i].hour && minute >= schedule[i].minute)) {
        return schedule[i].temperature;
      }
    }
  }

  return 0.0;
}

void ThermostatController::handle() {
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

void ThermostatController::setOn(bool enabled) {
  on = enabled;
  handle();  // Вызов handle при изменении состояния системы
}

bool ThermostatController::isOn() const {
  return on;
}

void ThermostatController::setRelay(bool newState) {
  if (relayState != newState) {
    relayState = newState;
    relayControl(newState);
  }
}

float ThermostatController::getCurrentTemperature() {
  float t = requestTemperature();
  if (t < THERMOSTAT_MIN_TEMPERATURE || t > THERMOSTAT_MAX_TEMPERATURE) {
    return 0.0;
  }
  return t;
}

void ThermostatController::fillState(ThermostatState* _state) const {
  _state->on = on;
  _state->relayState = on ? relayState : false;
  _state->currentTemperature = on ? currentTemperature : 0.0f;
  _state->desiredTemperature = on ? desiredTemperature : 0.0f;
}

bool ThermostatController::isSameState(const ThermostatState& a, const ThermostatState& b) const {
  static const float STATE_EPS = 0.01f;
  return
    a.on == b.on &&
    a.relayState == b.relayState &&
    fabsf(a.currentTemperature - b.currentTemperature) < STATE_EPS &&
    fabsf(a.desiredTemperature - b.desiredTemperature) < STATE_EPS;
}

void ThermostatController::notifyStateChangedIfNeeded() {
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

void ThermostatController::getState(ThermostatState* _state) {
  fillState(_state);
}

void ThermostatController::setStateChangedCallback(std::function<void(const ThermostatState&)> callback) {
  stateChangedCallback = callback;
  notifyStateChangedIfNeeded();
}

// Применение настроек из структуры ThermostatSettings (для загрузки из ПЗУ)
void ThermostatController::applySettings(const ThermostatSettings& _settings) {
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
ThermostatSettings ThermostatController::getSettings() const {
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
