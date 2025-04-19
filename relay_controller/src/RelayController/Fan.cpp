#include "Fan.h"
#include <Arduino.h>

// Конструктор
Fan::Fan(std::function<void(bool)> controlFunction, unsigned long defaultDurationMinutes)
  : relayControl(controlFunction), 
    isOn(false), 
    turnOffTime(0), 
    timerActive(false), 
    defaultTimerDuration(defaultDurationMinutes * 60 * 1000) {
  // Начальное состояние - выключено
  updateState(false);
}

// Приватный метод для обновления состояния и вызова управляющей функции
void Fan::updateState(bool state) {
  isOn = state;
  relayControl(state);
}

// Включить вентилятор навсегда
void Fan::turnOn() {
  timerActive = false;
  updateState(true);
}

// Выключить вентилятор
void Fan::turnOff() {
  timerActive = false;
  updateState(false);
}

// Включить вентилятор на определенное время
void Fan::turnOnWithTimer(unsigned long durationMinutes) {
  timerActive = true;
  
  unsigned long duration = (durationMinutes > 0) ? 
                          (durationMinutes * 60 * 1000) : 
                          defaultTimerDuration;
  
  turnOffTime = millis() + duration;
  updateState(true);
}

// Переключить состояние вентилятора
void Fan::toggle() {
  if (isOn) {
    turnOff();
  } else {
    turnOnWithTimer();
  }
}

// Обработка таймера
void Fan::handle() {
  if (timerActive && isOn && millis() >= turnOffTime) {
    turnOff();
  }
}

// Получить текущее состояние вентилятора
bool Fan::getState() const {
  return isOn;
}

// Получить оставшееся время работы таймера (в секундах)
long Fan::getRemainingTime() const {
  if (timerActive && isOn) {
    long remaining = (turnOffTime - millis()) / 1000;
    return (remaining > 0) ? remaining : 0;
  }
  return 0;
} 
