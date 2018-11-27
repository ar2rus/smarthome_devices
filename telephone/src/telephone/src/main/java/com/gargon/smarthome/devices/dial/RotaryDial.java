package com.gargon.smarthome.devices.dial;

import com.pi4j.io.gpio.GpioController;
import com.pi4j.io.gpio.GpioFactory;
import com.pi4j.io.gpio.GpioPinDigitalInput;
import com.pi4j.io.gpio.Pin;
import com.pi4j.io.gpio.PinPullResistance;
import com.pi4j.io.gpio.PinState;
import com.pi4j.io.gpio.RaspiPin;
import com.pi4j.io.gpio.event.GpioPinDigitalStateChangeEvent;
import com.pi4j.io.gpio.event.GpioPinListenerDigital;
import java.util.ArrayList;
import java.util.List;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 *
 * @author gargon
 */
public class RotaryDial {
    
    private static final Pin DIAL_ACTIVATE_PIN = RaspiPin.GPIO_02;
    private static final Pin DIAL_IMPULSE_PIN  = RaspiPin.GPIO_03;
    private static final Pin DIAL_CRADLE_PIN   = RaspiPin.GPIO_04;
    
    private static final int MAX_DELAY_BETWEEN_DIGITS = 1500;   //ms
    
    private static final int IMPULSE_MIN_DURATION = 5;   //ms, 30 ms in fact
    private static final int IMPULSE_MAX_DURATION = 150;   //ms
    
    private static final int IMPULSE_INTERSPACE_MIN_DURATION = 25;   //ms, 75 ms in fact
    private static final int IMPULSE_INTERSPACE_MAX_DURATION = 350;   //ms
    
    
    public static enum EVENT{
        PICKED_UP,           //трубка поднята, рычаг отпущен
        PUT_DOWN,            //трубка лежит, рычаг нажат
        DIAL_ACTIVATED,      //начат набор цифры, диск повернут
        DIAL_INACTIVATED,    //набор цифры завершен, диск в исходном положении
        DIGIT_DIALED,        //набрана цифра (в аргументе (тип byte): цифра от 0 до 9 или -1, если цифра не была набрана)
        NUMBER_DIAL_STARTED, //начат набор номера
        NUMBER_DIALED,       //набран номер (в аргументе (тип byte[]): массив цифр от 0 до 9 
                             //или null, если набор номера не был удачно завершен или был прерван например положенной трубкой)
    }
    
    //Последовательность выдачи событий при классическом наборе номера, например "02":
    //1. PICKED_UP
    //2. DIAL_ACTIVATED
    //3. NUMBER_DIAL_STARTED
    //4. DIAL_INACTIVATED
    //5. DIGIT_DIALED, 0
    //6. DIAL_ACTIVATED
    //7. DIAL_INACTIVATED
    //8. DIGIT_DIALED, 2
    //9. NUMBER_DIALED, [0,2]
    //10.PUT_DOWN
    
    private volatile boolean enabled = false;
    
    private List<Byte> number;
    private volatile boolean cradle_state;   //false - put down;  true - picked up
    private volatile boolean dial_state;     //false - inactive;  true - active
    private volatile int dial_impulse_count; //impulse(digit) counter
    
    private final List<RotaryDialListener> listeners = new CopyOnWriteArrayList();

    private static final Logger LOG = Logger.getLogger(RotaryDial.class.getName());

    private boolean dial_state(PinState state){
        return state == PinState.LOW;
    }
    
    private boolean cradle_state(PinState state){
        return state == PinState.LOW;
    }
    
    private boolean impulse(PinState state){
        return state == PinState.HIGH;
    }
    
    private final GpioPinDigitalInput dialActivatePin;
    private final GpioPinDigitalInput dialImpulsePin;
    private final GpioPinDigitalInput dialCradlePin;
    
    
    public RotaryDial() {
        final GpioController gpio = GpioFactory.getInstance();

        dialActivatePin = gpio.provisionDigitalInputPin(DIAL_ACTIVATE_PIN);
        dialActivatePin.setShutdownOptions(true);

        dialImpulsePin = gpio.provisionDigitalInputPin(DIAL_IMPULSE_PIN, PinPullResistance.PULL_UP);
        dialImpulsePin.setShutdownOptions(true);

        dialCradlePin = gpio.provisionDigitalInputPin(DIAL_CRADLE_PIN, PinPullResistance.PULL_UP);
        dialCradlePin.setShutdownOptions(true);
        dialCradlePin.setDebounce(100);

        LOG.log(Level.INFO, "Rotary dial driver initiated");
        
        
         dialActivatePin.addListener(new GpioPinListenerDigital() {

             Timer timer = new Timer();
             TimerTask numberReadyTask = null;
             
            @Override
            public void handleGpioPinDigitalStateChangeEvent(GpioPinDigitalStateChangeEvent event) {
                if (enabled) {
                    if (numberReadyTask == null || numberReadyTask.cancel()) {
                        dial_state = dial_state(event.getState());
                        sendEvent(dial_state ? EVENT.DIAL_ACTIVATED : EVENT.DIAL_INACTIVATED);

                        if (dial_state) {    //начало набора цифры
                            dial_impulse_count = 0; //обнуляем счетчик импульсов

                            if (number == null) {    //начало набора номера
                                number = new ArrayList();
                                sendEvent(EVENT.NUMBER_DIAL_STARTED);
                            }
                        } else {  //набор цифры завершен
                            if (number != null) {   //был сделан сброс кнопкой
                                byte digit_code = -1;   //вычисляем код набранной цифры по кол-ву импульсов
                                if (dial_impulse_count > 0 && dial_impulse_count <= 10) {
                                    digit_code = (byte) (dial_impulse_count % 10);
                                }
                                sendEvent(EVENT.DIGIT_DIALED, digit_code);

                                if (digit_code >= 0) {   //набрана корректная цифра, добавляем в номер
                                    number.add(digit_code);

                                    numberReadyTask = new TimerTask() {
                                        @Override
                                        public void run() {
                                            sendEvent(EVENT.NUMBER_DIALED, number);
                                            number = null;
                                            numberReadyTask = null;
                                        }
                                    };
                                    timer.schedule(numberReadyTask, MAX_DELAY_BETWEEN_DIGITS);

                                } else {  //ошибка при наборе цифры -> сбрасываем весь номер
                                    number = null;
                                    sendEvent(EVENT.NUMBER_DIALED, number);
                                }
                            }
                        }
                    }
                }
            }
        });
         
         
         dialImpulsePin.addListener(new GpioPinListenerDigital() {
            @Override
            public void handleGpioPinDigitalStateChangeEvent(GpioPinDigitalStateChangeEvent event) {
                if (enabled && dial_state){
                    if (impulse(event.getState())){
                        //TODO: анализировать длительность импульсов
                        dial_impulse_count++;
                    }
                }
            }
        });
        
         dialCradlePin.addListener(new GpioPinListenerDigital() {
            @Override
            public void handleGpioPinDigitalStateChangeEvent(GpioPinDigitalStateChangeEvent event) {
                cradle_state = cradle_state(event.getState());
                sendEvent(cradle_state ? EVENT.PICKED_UP : EVENT.PUT_DOWN);
                
                if (number != null){//TODO: add synch?
                    if (!cradle_state){ //положили трубку во время набора -> сбрасываем набор
                        number = null;  //если трубку сняли во время набора -> номер не сбрасывется
                        sendEvent(EVENT.NUMBER_DIALED, number);
                    }
                }
                
            }
        });
         
        cradle_state = cradle_state(dialCradlePin.getState());
        enabled = true;
    }
    
    public void addListener(RotaryDialListener listener) {
        if (listener != null) {
            listeners.add(listener);
        }
    }

    public void removeListener(RotaryDialListener listener) {
        if (listener != null) {
            listeners.remove(listener);
        }
    }

    private void sendEvent(EVENT event, Object... arg) {
        for (RotaryDialListener listener : listeners) {
            listener.handleRotaryDialEvent(event, arg);
        }
    }


    public boolean isCradle_state() {
        return cradle_state;
    }

}
