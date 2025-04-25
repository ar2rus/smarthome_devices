#include "heatfloor.h"

#include <time.h>

// Конструктор с передачей настроек
FloorHeatingController::FloorHeatingController(const FloorHeatingSettings& _settings, 
                                              std::function<float()> _requestTemperature, 
                                              std::function<void(bool)> _relayControl)
  : requestTemperature(_requestTemperature), relayControl(_relayControl), relayState(false) {
  
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
  if (!on) {
    setRelay(false);
    return;
  }
    
  time_t t;
  time(&t);
  struct tm* lt = localtime(&t);
  
  desiredTemperature = getDesiredTemperature(lt->tm_hour, lt->tm_min, lt->tm_wday);
  if (desiredTemperature == 0.0) {
    setRelay(false);
    return;
  }
  
  currentTemperature = getCurrentTemperature();
  if (currentTemperature == 0.0) {
    setRelay(false);
    return;
  }

  if (currentTemperature <= desiredTemperature - HEATFLOOR_TEMPERATURE_HYSTERESIS) {
    setRelay(true);
  } else if (currentTemperature >= desiredTemperature + HEATFLOOR_TEMPERATURE_HYSTERESIS) {
    setRelay(false);
  }
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

void FloorHeatingController::getState(FloorHeatingState* _state) {
  _state->on = on;
  if (on) {
    _state->relayState = relayState;
    _state->currentTemperature = currentTemperature;
    _state->desiredTemperature = desiredTemperature;
  }
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
