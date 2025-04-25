#ifndef FLOOR_HEATING_CONTROLLER_H
#define FLOOR_HEATING_CONTROLLER_H

using namespace std;
#include <functional>
#include <vector>
#include <string>

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

// Структура с настройками теплого пола, включающая расписание и состояние
// Используется только для инициализации
struct FloorHeatingSettings {
  std::vector<FloorHeatingSchedule> schedule;
  bool enabled;
  
  FloorHeatingSettings() : enabled(true) {}
  
  FloorHeatingSettings(const std::vector<FloorHeatingSchedule>& _schedule, bool _enabled) 
    : schedule(_schedule), enabled(_enabled) {}
  
  // Конструктор из массива расписаний
  FloorHeatingSettings(const FloorHeatingSchedule* _scheduleArray, int _size, bool _enabled)
    : enabled(_enabled) {
    schedule.assign(_scheduleArray, _scheduleArray + _size);
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
    int scheduleSize;
    
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
    // Конструктор с передачей настроек
    FloorHeatingController(const FloorHeatingSettings& _settings, 
                          std::function<float()> _requestTemperature, 
                          std::function<void(bool)> _relayControl);
    
    // Включение/выключение системы
    void setOn(bool enabled);
    
    // Получение текущего состояния (включена/выключена)
    bool isOn() const;
    
    // Основная функция обработки
    void handle();
    
    // Получение текущего состояния для отображения
    void getState(FloorHeatingState* _state);
    
    // Применение настроек из структуры FloorHeatingSettings (для загрузки из ПЗУ)
    void applySettings(const FloorHeatingSettings& _settings);
    
    // Получение текущих настроек в виде FloorHeatingSettings (для сохранения в ПЗУ)
    FloorHeatingSettings getSettings() const;
    
    // Деструктор для освобождения памяти
    ~FloorHeatingController();
};

#endif // FLOOR_HEATING_CONTROLLER_H
