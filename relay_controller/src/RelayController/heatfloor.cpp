#include "heatfloor.h"

#include <time.h>
#include <math.h>

// Конструктор с передачей настроек
FloorHeatingController::FloorHeatingController(const FloorHeatingSettings& _settings, 
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
  schedule = new FloorHeatingSchedule[scheduleSize];
  for (int i = 0; i < scheduleSize; i++) {
    schedule[i] = _settings.schedule[i];
  }
  on = _settings.enabled;
}

// Деструктор для освобождения памяти
FloorHeatingController::~FloorHeatingController() {
  if (schedule != nullptr) {
    delete[] schedule;
  }
}

float FloorHeatingController::getDesiredTemperature(int hour, int minute, int dayOfWeek) {
  for (int i = scheduleSize - 1; i >= 0; i--) {
    if (schedule[i].checkDayOfWeek(dayOfWeek)) {
      if (hour > schedule[i].hour || (hour == schedule[i].hour && minute >= schedule[i].minute)) {
        return schedule[i].temperature;
      }
    }
  }

  return 0.0;
}

void FloorHeatingController::handle() {
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

  if (currentTemperature <= desiredTemperature - HEATFLOOR_TEMPERATURE_HYSTERESIS) {
    setRelay(true);
  } else if (currentTemperature >= desiredTemperature + HEATFLOOR_TEMPERATURE_HYSTERESIS) {
    setRelay(false);
  }

  notifyStateChangedIfNeeded();
}

void FloorHeatingController::setOn(bool enabled) {
  on = enabled;
  handle();  // Вызов handle при изменении состояния системы
}

bool FloorHeatingController::isOn() const {
  return on;
}

void FloorHeatingController::setRelay(bool newState) {
  if (relayState != newState) {
    relayState = newState;
    relayControl(newState);
  }
}

float FloorHeatingController::getCurrentTemperature() {
  float t = requestTemperature();
  if (t < HEATFLOOR_MIN_TEMPERATURE || t > HEATFLOOR_MAX_TEMPERATURE) {
    return 0.0;
  }
  return t;
}

void FloorHeatingController::fillState(FloorHeatingState* _state) const {
  _state->on = on;
  _state->relayState = on ? relayState : false;
  _state->currentTemperature = on ? currentTemperature : 0.0f;
  _state->desiredTemperature = on ? desiredTemperature : 0.0f;
}

bool FloorHeatingController::isSameState(const FloorHeatingState& a, const FloorHeatingState& b) const {
  static const float STATE_EPS = 0.01f;
  return
    a.on == b.on &&
    a.relayState == b.relayState &&
    fabsf(a.currentTemperature - b.currentTemperature) < STATE_EPS &&
    fabsf(a.desiredTemperature - b.desiredTemperature) < STATE_EPS;
}

void FloorHeatingController::notifyStateChangedIfNeeded() {
  FloorHeatingState currentState;
  fillState(&currentState);

  if (!lastReportedStateValid || !isSameState(currentState, lastReportedState)) {
    lastReportedState = currentState;
    lastReportedStateValid = true;
    if (stateChangedCallback) {
      stateChangedCallback(currentState);
    }
  }
}

void FloorHeatingController::getState(FloorHeatingState* _state) {
  fillState(_state);
}

void FloorHeatingController::setStateChangedCallback(std::function<void(const FloorHeatingState&)> callback) {
  stateChangedCallback = callback;
  notifyStateChangedIfNeeded();
}

// Применение настроек из структуры FloorHeatingSettings (для загрузки из ПЗУ)
void FloorHeatingController::applySettings(const FloorHeatingSettings& _settings) {
  // Освобождаем старое расписание
  delete[] schedule;
  
  // Копируем расписание из settings в локальные поля
  scheduleSize = _settings.schedule.size();
  schedule = new FloorHeatingSchedule[scheduleSize];
  for (int i = 0; i < scheduleSize; i++) {
    schedule[i] = _settings.schedule[i];
  }
  on = _settings.enabled;
  handle();
}

// Получение текущих настроек в виде FloorHeatingSettings (для сохранения в ПЗУ)
FloorHeatingSettings FloorHeatingController::getSettings() const {
  // Создаем новый объект настроек
  FloorHeatingSettings settings;
  
  // Заполняем расписание
  settings.schedule.clear();
  for (int i = 0; i < scheduleSize; i++) {
    settings.schedule.push_back(schedule[i]);
  }
  
  // Заполняем состояние включения
  settings.enabled = on;
  
  return settings;
}
