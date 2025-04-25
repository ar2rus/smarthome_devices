#include "FanController.h"
#include <Arduino.h>

// Конструктор
FanController::FanController(std::function<void(bool)> controlFunction, unsigned long defaultDurationMinutes)
  : relayControl(controlFunction), 
    isOn(false), 
    turnOffTime(0), 
    timerActive(false), 
    defaultTimerDuration(defaultDurationMinutes * 60 * 1000) {
  // Начальное состояние - выключено
  updateState(false);
}

// Приватный метод для обновления состояния и вызова управляющей функции
void FanController::updateState(bool state) {
  isOn = state;
  relayControl(state);
}

// Включить вентилятор навсегда
void FanController::turnOn() {
  timerActive = false;
  updateState(true);
}

// Выключить вентилятор
void FanController::turnOff() {
  timerActive = false;
  updateState(false);
}

// Включить вентилятор на определенное время
void FanController::turnOnWithTimer(unsigned long durationMinutes) {
  timerActive = true;
  
  unsigned long duration = (durationMinutes > 0) ? 
                          (durationMinutes * 60 * 1000) : 
                          defaultTimerDuration;
  
  turnOffTime = millis() + duration;
  updateState(true);
}

// Переключить состояние вентилятора
void FanController::toggle() {
  if (isOn) {
    turnOff();
  } else {
    turnOnWithTimer();
  }
}

// Обработка таймера
void FanController::handle() {
  if (timerActive && isOn && millis() >= turnOffTime) {
    turnOff();
  }
}

// Получить текущее состояние вентилятора
bool FanController::getState() const {
  return isOn;
}

// Получить оставшееся время работы таймера (в секундах)
long FanController::getRemainingTime() const {
  if (timerActive && isOn) {
    long remaining = (turnOffTime - millis()) / 1000;
    return (remaining > 0) ? remaining : 0;
  }
  return 0;
} 