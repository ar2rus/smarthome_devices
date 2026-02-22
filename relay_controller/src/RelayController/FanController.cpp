#include "FanController.h"
#include <Arduino.h>

// Конструктор
FanController::FanController(std::function<void(bool)> controlFunction, unsigned long defaultDurationMinutes)
  : relayControl(controlFunction), 
    on(true),
    relayState(false),
    turnOffTime(0), 
    timerActive(false), 
    defaultTimerDuration(defaultDurationMinutes * 60 * 1000),
    stateChangedCallback(nullptr),
    lastReportedState{true, false, 0},
    lastReportedStateValid(false) {
  // Начальное состояние - выключено
  relayControl(false);
  notifyStateChanged(true);
}

bool FanController::isSameState(const FloorControllerState& a, const FloorControllerState& b) const {
  return a.on == b.on && a.relayState == b.relayState && a.remainingTime == b.remainingTime;
}

void FanController::notifyStateChanged(bool force) {
  FloorControllerState state = getState();
  if (!force && lastReportedStateValid && isSameState(lastReportedState, state)) {
    return;
  }

  lastReportedState = state;
  lastReportedStateValid = true;

  if (stateChangedCallback) {
    stateChangedCallback(state);
  }
}

// Приватный метод для обновления состояния и вызова управляющей функции
void FanController::updateRelayState(bool state) {
  bool appliedState = on ? state : false;
  if (relayState != appliedState) {
    relayState = appliedState;
    relayControl(appliedState);
  }
  notifyStateChanged();
}

void FanController::setOn(bool enabled) {
  if (on == enabled) {
    return;
  }

  on = enabled;
  if (!on) {
    timerActive = false;
    updateRelayState(false);
    return;
  }

  notifyStateChanged(true);
}

bool FanController::isOn() const {
  return on;
}

// Включить вентилятор навсегда
void FanController::turnOn() {
  if (!on) {
    return;
  }
  timerActive = false;
  updateRelayState(true);
}

// Выключить вентилятор
void FanController::turnOff() {
  timerActive = false;
  updateRelayState(false);
}

// Включить вентилятор на определенное время
void FanController::turnOnWithTimer(unsigned long durationMinutes) {
  if (!on) {
    return;
  }

  timerActive = true;
  
  unsigned long duration = (durationMinutes > 0) ? 
                          (durationMinutes * 60 * 1000) : 
                          defaultTimerDuration;
  
  turnOffTime = millis() + duration;
  updateRelayState(true);
}

// Переключить состояние вентилятора
void FanController::toggle() {
  if (!on) {
    return;
  }

  if (relayState) {
    turnOff();
  } else {
    turnOnWithTimer();
  }
}

// Обработка таймера
void FanController::handle() {
  if (!on) {
    notifyStateChanged();
    return;
  }

  if (timerActive && relayState && millis() >= turnOffTime) {
    turnOff();
    return;
  }

  if (timerActive && relayState) {
    notifyStateChanged();
  }
}

// Получить оставшееся время работы таймера (в секундах)
long FanController::getRemainingTime() const {
  if (timerActive && relayState && on) {
    long remaining = (turnOffTime - millis()) / 1000;
    return (remaining > 0) ? remaining : 0;
  }
  return 0;
}

FloorControllerState FanController::getState() const {
  FloorControllerState state;
  state.on = on;
  state.relayState = on ? relayState : false;
  state.remainingTime = getRemainingTime();
  return state;
}

void FanController::setStateChangedCallback(std::function<void(const FloorControllerState&)> callback) {
  stateChangedCallback = callback;
  notifyStateChanged(true);
}
