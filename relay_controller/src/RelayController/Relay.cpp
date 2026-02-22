#include "Relay.h"
#include <Arduino.h>

// Конструктор
Relay::Relay(std::function<void(bool)> controlFunction, unsigned long defaultDurationMinutes)
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

bool Relay::isSameState(const RelayState& a, const RelayState& b) const {
  return a.on == b.on && a.relayState == b.relayState && a.remainingTime == b.remainingTime;
}

void Relay::notifyStateChanged(bool force) {
  RelayState state = getState();
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
void Relay::updateRelayState(bool state) {
  bool appliedState = on ? state : false;
  if (relayState != appliedState) {
    relayState = appliedState;
    relayControl(appliedState);
  }
  notifyStateChanged();
}

void Relay::setOn(bool enabled) {
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

bool Relay::isOn() const {
  return on;
}

// Включить реле навсегда
void Relay::turnOn() {
  if (!on) {
    return;
  }
  timerActive = false;
  updateRelayState(true);
}

// Выключить реле
void Relay::turnOff() {
  timerActive = false;
  updateRelayState(false);
}

// Включить реле на определенное время
void Relay::turnOnWithTimer(unsigned long durationMinutes) {
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

// Переключить состояние реле
void Relay::toggle() {
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
void Relay::handle() {
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
long Relay::getRemainingTime() const {
  if (timerActive && relayState && on) {
    long remaining = (turnOffTime - millis()) / 1000;
    return (remaining > 0) ? remaining : 0;
  }
  return 0;
}

RelayState Relay::getState() const {
  RelayState state;
  state.on = on;
  state.relayState = on ? relayState : false;
  state.remainingTime = getRemainingTime();
  return state;
}

void Relay::setStateChangedCallback(std::function<void(const RelayState&)> callback) {
  stateChangedCallback = callback;
  notifyStateChanged(true);
}
