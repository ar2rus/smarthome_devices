#ifndef FLOOR_HEATING_CONTROLLER_H
#define FLOOR_HEATING_CONTROLLER_H

using namespace std;
#include <functional>

#define HEATFLOOR_MIN_TEMPERATURE 10
#define HEATFLOOR_MAX_TEMPERATURE 45

#define HEATFLOOR_TEMPERATURE_HYSTERESIS 0.3

struct FloorHeatingSchedule {
  int hour;
  int minute;
  float temperature;
  int dayOfWeek;  // 0-6: воскресенье-суббота, -1: любой день, -2: будние дни, -3: выходные

  bool checkDayOfWeek(int dw){
    return dayOfWeek == -1 ||  // Любой день
           dayOfWeek == dw ||  // Конкретный день недели
           (dayOfWeek == -2 && dw != 0 && dw != 6) ||  // Будние дни
           (dayOfWeek == -3 && (dw == 0 || dw == 6));  // Выходные
  }
};

struct FloorHeatingState {
  bool on;  // Глобальное включение/выключение системы
  bool relayState;  // Текущее состояние реле
  float currentTemperature;  // Текущая температура
  float desiredTemperature;  // Текущая целевая температура
};

class FloorHeatingController {
  private:
    
    FloorHeatingSchedule* schedule;
    const int scheduleSize;
    
    bool on;  // Глобальное включение/выключение системы
    bool relayState;  // Текущее состояние реле
    float currentTemperature;  // Текущая температура
    float desiredTemperature;  // Текущая целевая температура
    
	  std::function<void(bool)> relayControl;
    std::function<float()> requestTemperature;

    void setRelay(bool newState);
    float getCurrentTemperature();
    float getDesiredTemperature(int hour, int minute, int dayOfWeek);

  public:
    FloorHeatingController(FloorHeatingSchedule* _schedule, int _scheduleSize, std::function<float()> _requestTemperature, std::function<void(bool)> _relayControl);
    
    void setOn(bool enabled);
	  void handle();
    
    void getState(FloorHeatingState* _state);
};

#endif // FLOOR_HEATING_CONTROLLER_H
