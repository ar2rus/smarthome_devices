#ifndef FANCONTROLLER_H
#define FANCONTROLLER_H

#include <functional>

struct FloorControllerState {
  bool on;
  bool relayState;
  long remainingTime;
};

class FanController {
  private:
    // Функция для управления реле вентилятора
    std::function<void(bool)> relayControl;
    
    // Глобальное состояние контроллера (при false внешние команды игнорируются)
    bool on;

    // Текущее физическое состояние реле вентилятора
    bool relayState;
    
    // Время в миллисекундах, когда нужно выключить вентилятор
    unsigned long turnOffTime;
    
    // Установлен ли таймер
    bool timerActive;
    
    // Длительность таймера по умолчанию (в миллисекундах)
    const unsigned long defaultTimerDuration;

    // Колбэк изменения состояния/оставшегося времени
    std::function<void(const FloorControllerState&)> stateChangedCallback;
    FloorControllerState lastReportedState;
    bool lastReportedStateValid;
    
    // Приватный метод для обновления состояния и вызова управляющей функции
    void updateRelayState(bool state);
    bool isSameState(const FloorControllerState& a, const FloorControllerState& b) const;
    void notifyStateChanged(bool force = false);
    
  public:
    // Конструктор принимает функцию для управления реле и время таймера по умолчанию
    FanController(std::function<void(bool)> controlFunction, unsigned long defaultDurationMinutes = 30);

    // Глобально включить/выключить контроллер
    void setOn(bool enabled);
    bool isOn() const;
    
    // Включить вентилятор навсегда
    void turnOn();
    
    // Выключить вентилятор
    void turnOff();
    
    // Включить вентилятор на определенное время (в минутах)
    void turnOnWithTimer(unsigned long durationMinutes = 0);
    
    // Переключить состояние вентилятора
    void toggle();
    
    // Обработка таймера, должна вызываться в loop
    void handle();
    
    // Получить оставшееся время работы таймера (в секундах)
    long getRemainingTime() const;

    // Получить состояние контроллера вентилятора
    FloorControllerState getState() const;

    // Установить колбэк изменения состояния/оставшегося времени
    void setStateChangedCallback(std::function<void(const FloorControllerState&)> callback);
};

#endif // FANCONTROLLER_H 
