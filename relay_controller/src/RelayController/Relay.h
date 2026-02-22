#ifndef RELAY_H
#define RELAY_H

#include <functional>

struct RelayState {
  bool on;
  bool relayState;
  long remainingTime;
};

class Relay {
  private:
    // Функция для управления реле
    std::function<void(bool)> relayControl;
    
    // Глобальное состояние контроллера (при false внешние команды игнорируются)
    bool on;

    // Текущее физическое состояние реле
    bool relayState;
    
    // Время в миллисекундах, когда нужно выключить реле
    unsigned long turnOffTime;
    
    // Установлен ли таймер
    bool timerActive;
    
    // Длительность таймера по умолчанию (в миллисекундах)
    const unsigned long defaultTimerDuration;

    // Колбэк изменения состояния/оставшегося времени
    std::function<void(const RelayState&)> stateChangedCallback;
    RelayState lastReportedState;
    bool lastReportedStateValid;
    
    // Приватный метод для обновления состояния и вызова управляющей функции
    void updateRelayState(bool state);
    bool isSameState(const RelayState& a, const RelayState& b) const;
    void notifyStateChanged(bool force = false);
    
  public:
    // Конструктор принимает функцию для управления реле и время таймера по умолчанию
    Relay(std::function<void(bool)> controlFunction, unsigned long defaultDurationMinutes = 30);

    // Глобально включить/выключить контроллер
    void setOn(bool enabled);
    bool isOn() const;
    
    // Включить реле навсегда
    void turnOn();
    
    // Выключить реле
    void turnOff();
    
    // Включить реле на определенное время (в минутах)
    void turnOnWithTimer(unsigned long durationMinutes = 0);
    
    // Переключить состояние реле
    void toggle();
    
    // Обработка таймера, должна вызываться в loop
    void handle();
    
    // Получить оставшееся время работы таймера (в секундах)
    long getRemainingTime() const;

    // Получить состояние контроллера реле
    RelayState getState() const;

    // Установить колбэк изменения состояния/оставшегося времени
    void setStateChangedCallback(std::function<void(const RelayState&)> callback);
};

#endif // RELAY_H 
