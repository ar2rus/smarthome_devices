#ifndef THERMOSTAT_H
#define THERMOSTAT_H

using namespace std;
#include <functional>
#include <vector>
#include <string>

#define THERMOSTAT_MIN_TEMPERATURE 10
#define THERMOSTAT_MAX_TEMPERATURE 45

#define THERMOSTAT_TEMPERATURE_HYSTERESIS 0.3

struct ThermostatSchedule {
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
struct ThermostatSettings {
  std::vector<ThermostatSchedule> schedule;
  bool enabled;
  
  ThermostatSettings() : enabled(true) {}
  
  ThermostatSettings(const std::vector<ThermostatSchedule>& _schedule, bool _enabled) 
    : schedule(_schedule), enabled(_enabled) {}
  
  // Конструктор из массива расписаний
  ThermostatSettings(const ThermostatSchedule* _scheduleArray, int _size, bool _enabled)
    : enabled(_enabled) {
    schedule.assign(_scheduleArray, _scheduleArray + _size);
  }
};

struct ThermostatState {
  bool on;  // Глобальное включение/выключение системы
  bool relayState;  // Текущее состояние реле
  float currentTemperature;  // Текущая температура
  float desiredTemperature;  // Текущая целевая температура
};

class Thermostat {
  private:
    ThermostatSchedule* schedule;
    int scheduleSize;
    
    bool on;  // Глобальное включение/выключение системы
    bool relayState;  // Текущее состояние реле
    float currentTemperature;  // Текущая температура
    float desiredTemperature;  // Текущая целевая температура
    
    std::function<void(bool)> relayControl;
    std::function<float()> requestTemperature;
    std::function<void(const ThermostatState&)> stateChangedCallback;
    ThermostatState lastReportedState;
    bool lastReportedStateValid;

    void setRelay(bool newState);
    float getCurrentTemperature();
    float getDesiredTemperature(int hour, int minute, int dayOfWeek);
    void fillState(ThermostatState* _state) const;
    bool isSameState(const ThermostatState& a, const ThermostatState& b) const;
    void notifyStateChangedIfNeeded();

  public:
    // Конструктор с передачей настроек
    Thermostat(const ThermostatSettings& _settings, 
                          std::function<float()> _requestTemperature, 
                          std::function<void(bool)> _relayControl);
    
    // Включение/выключение системы
    void setOn(bool enabled);
    
    // Получение текущего состояния (включена/выключена)
    bool isOn() const;
    
    // Основная функция обработки
    void handle();
    
    // Получение текущего состояния для отображения
    void getState(ThermostatState* _state);

    // Установить callback изменения состояния
    void setStateChangedCallback(std::function<void(const ThermostatState&)> callback);
    
    // Применение настроек из структуры ThermostatSettings (для загрузки из ПЗУ)
    void applySettings(const ThermostatSettings& _settings);
    
    // Получение текущих настроек в виде ThermostatSettings (для сохранения в ПЗУ)
    ThermostatSettings getSettings() const;
    
    // Деструктор для освобождения памяти
    ~Thermostat();
};

#endif // THERMOSTAT_H
