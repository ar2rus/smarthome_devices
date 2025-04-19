#include "heatfloor.h"

#include <time.h>

FloorHeatingController::FloorHeatingController(FloorHeatingSchedule* _schedule, int _scheduleSize, std::function<float()> _requestTemperature, std::function<void(bool)> _relayControl)
  : schedule(_schedule), scheduleSize(_scheduleSize), requestTemperature(_requestTemperature), relayControl(_relayControl), relayState(false), on(true) {}

float FloorHeatingController::getDesiredTemperature(int hour, int minute, int dayOfWeek) {
  
  for (int i=scheduleSize-1; i>=0; i--){
    if (schedule[i].checkDayOfWeek(dayOfWeek)){
      if (hour > schedule[i].hour || (hour == schedule[i].hour && minute >= schedule[i].minute)){
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
  if (currentTemperature == 0.0){
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

void FloorHeatingController::setRelay(bool newState) {
  if (relayState != newState) {
    relayState = newState;
    relayControl(newState);
  }
}

float FloorHeatingController::getCurrentTemperature(){
  float t = requestTemperature();
  if (t < HEATFLOOR_MIN_TEMPERATURE || t > HEATFLOOR_MAX_TEMPERATURE){
      return 0.0;
  }
  return t;
}

void FloorHeatingController::getState(FloorHeatingState* _state){
  _state->on = on;
  if (on){
    _state->relayState = relayState;
    _state->currentTemperature = currentTemperature;
    _state->desiredTemperature = desiredTemperature;
  }
}
