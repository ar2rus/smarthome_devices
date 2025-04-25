#ifndef FANCONTROLLER_H
#define FANCONTROLLER_H

#include <functional>

class FanController {
  private:
    // Функция для управления реле вентилятора
    std::function<void(bool)> relayControl;
    
    // Состояние вентилятора (вкл/выкл)
    bool isOn;
    
    // Время в миллисекундах, когда нужно выключить вентилятор
    unsigned long turnOffTime;
    
    // Установлен ли таймер
    bool timerActive;
    
    // Длительность таймера по умолчанию (в миллисекундах)
    const unsigned long defaultTimerDuration;
    
    // Приватный метод для обновления состояния и вызова управляющей функции
    void updateState(bool state);
    
  public:
    // Конструктор принимает функцию для управления реле и время таймера по умолчанию
    FanController(std::function<void(bool)> controlFunction, unsigned long defaultDurationMinutes = 30);
    
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
    
    // Получить текущее состояние вентилятора
    bool getState() const;
    
    // Получить оставшееся время работы таймера (в секундах)
    long getRemainingTime() const;
};

#endif // FANCONTROLLER_H 