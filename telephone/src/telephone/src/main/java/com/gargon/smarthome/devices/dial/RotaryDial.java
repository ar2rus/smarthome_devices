package com.gargon.smarthome.devices.dial;

import com.pi4j.io.gpio.GpioController;
import com.pi4j.io.gpio.GpioFactory;
import com.pi4j.io.gpio.GpioPinDigitalInput;
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
    
    private static final int MAX_DELAY_BETWEEN_DIGITS = 2000;   //ms
    
    private static final int IMPULSE_MIN_DURATION = 5;   //ms, 60 ms in fact
    private static final int IMPULSE_MAX_DURATION = 300;   //ms
    
    private static final int IMPULSE_INTERSPACE_MIN_DURATION = 5;   //ms, 40 ms in fact
    private static final int IMPULSE_INTERSPACE_MAX_DURATION = 250;   //ms
    
    private static final int CRADLE_DEBOUNCE = 100; //ms
    
    private static final boolean RESET_DIAL_BY_PICK_UP  = false; //сбрасывать текущий набор, если трубку подняли
    private static final boolean RESET_DIAL_BY_PUT_DOWN = true;  //сбрасывать теуший набор, если трубку положили 
    
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
    
    //После события DIAL_ACTIVATED всегда будут события DIAL_INACTIVATED и DIGIT_DIALED
    //А после события NUMBER_DIAL_STARTED всегда будет событие NUMBER_DIALED
    
    private boolean enabled = false;
    
    private List<Byte> number;
    private final Object numberLock = new Object();
    
    private TimerTask numberReadyTask = null;
    
    private volatile boolean cradle_state;   //false - put down;  true - picked up
    private volatile boolean dial_state;     //false - inactive;  true - active
    private volatile int dial_impulse_count; //impulse(digit) counter
    
    private final GpioPinDigitalInput dialActivatePin;
    private final GpioPinDigitalInput dialImpulsePin;
    private final GpioPinDigitalInput dialCradlePin;
    
    private final List<RotaryDialListener> listeners = new CopyOnWriteArrayList();

    private static final Logger LOG = Logger.getLogger(RotaryDial.class.getName());
    
    public RotaryDial(int cradle_pin, int dial_activate_pin, int dial_impulse_pin) {
        final GpioController gpio = GpioFactory.getInstance();

        dialActivatePin = gpio.provisionDigitalInputPin(RaspiPin.getPinByAddress(dial_activate_pin), PinPullResistance.PULL_UP);
        dialActivatePin.setShutdownOptions(true);

        dialImpulsePin = gpio.provisionDigitalInputPin(RaspiPin.getPinByAddress(dial_impulse_pin), PinPullResistance.PULL_UP);
        dialImpulsePin.setShutdownOptions(true);

        dialCradlePin = gpio.provisionDigitalInputPin(RaspiPin.getPinByAddress(cradle_pin), PinPullResistance.PULL_UP);
        dialCradlePin.setShutdownOptions(true);
        dialCradlePin.setDebounce(CRADLE_DEBOUNCE);

        LOG.log(Level.INFO, "Rotary dial driver initiated");
        
        
        dialActivatePin.addListener(new GpioPinListenerDigital() {

            Timer timer = new Timer();
             
            @Override
            public void handleGpioPinDigitalStateChangeEvent(GpioPinDigitalStateChangeEvent event) {
                if (enabled) {
                    synchronized (numberLock){
                    if (numberReadyTask == null || numberReadyTask.cancel()) {
                        numberReadyTask = null; // null it, if cancel
                        
                        dial_state = dial_state(event.getState());
                        sendEvent(dial_state ? EVENT.DIAL_ACTIVATED : EVENT.DIAL_INACTIVATED);

                        if (dial_state) {    //начало набора цифры
                            dial_impulse_count = 0; //обнуляем счетчик импульсов

                            if (number == null) {    //начало набора номера
                                number = new ArrayList();
                                sendEvent(EVENT.NUMBER_DIAL_STARTED);
                            }
                        } else {  //набор цифры завершен
                            
                                byte digit_code = -1;   //вычисляем код набранной цифры по кол-ву импульсов
                                if (dial_impulse_count > 0 && dial_impulse_count <= 10) {
                                    digit_code = (byte) (dial_impulse_count % 10);
                                }
                                sendEvent(EVENT.DIGIT_DIALED, digit_code);
                                if (number != null) {   //был сделан сброс
                                if (digit_code >= 0) {   //набрана корректная цифра, добавляем в номер
                                    number.add(digit_code);

                                    numberReadyTask = new TimerTask() {
                                        @Override
                                        public void run() {
                                            synchronized (numberLock) {
                                                if (number != null) {
                                                    sendEvent(EVENT.NUMBER_DIALED, number.toArray(new Object[0]));
                                                    number = null;
                                                    numberReadyTask = null;
                                                }
                                            }
                                        }
                                    };
                                    timer.schedule(numberReadyTask, MAX_DELAY_BETWEEN_DIGITS);

                                } else {  //ошибка при наборе цифры -> сбрасываем весь номер
                                    reset_dial();
                                }
                            }
                        }
                    }
                    }
                }
            }
        });
         
        dialImpulsePin.addListener(new GpioPinListenerDigital() {

            long impulse_start_time;

            @Override
            public void handleGpioPinDigitalStateChangeEvent(GpioPinDigitalStateChangeEvent event) {
                if (enabled && dial_state) {
                    long t = System.currentTimeMillis();
                    long dt = t - impulse_start_time;

                    boolean impulse = impulse(event.getState());

                    if (dial_impulse_count > 0) {
                        if (impulse) {
                            if (dt < IMPULSE_INTERSPACE_MIN_DURATION || dt > IMPULSE_INTERSPACE_MAX_DURATION) {
                                LOG.log(Level.WARNING, "Wrong interspace impulse duration: {0} ms", dt);
                                reset_dial();
                                return;
                            }
                        } else if (dt < IMPULSE_MIN_DURATION || dt > IMPULSE_MAX_DURATION) {
                            LOG.log(Level.WARNING, "Wrong impulse duration: {0} ms", dt);
                            reset_dial();
                            return;
                        }
                    }

                    if (impulse) {
                        dial_impulse_count++;
                    }
                    impulse_start_time = t;
                }
            }
        });
        
        dialCradlePin.addListener(new GpioPinListenerDigital() {
            @Override
            public void handleGpioPinDigitalStateChangeEvent(GpioPinDigitalStateChangeEvent event) {
                cradle_state = cradle_state(event.getState());
                if (enabled) {
                    sendEvent(cradle_state ? EVENT.PICKED_UP : EVENT.PUT_DOWN);

                    synchronized (numberLock) {
                        if (number != null) {
                            if ((!cradle_state && RESET_DIAL_BY_PUT_DOWN)
                                    || (cradle_state && RESET_DIAL_BY_PICK_UP)) {   //сбрасываем набор в зависимости от событий рычага
                                reset_dial();
                            }
                        }
                    }
                }
            }
        });
         
        cradle_state = cradle_state(dialCradlePin.getState());
    }
    
     private boolean dial_state(PinState state){
        return state == PinState.LOW;
    }
    
    private boolean cradle_state(PinState state){
        return state == PinState.LOW;
    }
    
    private boolean impulse(PinState state){
        return state == PinState.HIGH;
    }
    
    private void reset_dial() {
        boolean send = true;
        synchronized (numberLock) {
            if (numberReadyTask != null) {
                send = numberReadyTask.cancel();
                numberReadyTask = null;
            }
            number = null;
            
            dial_impulse_count = 0;
            dial_state = false;
            
            if (send) {
                sendEvent(EVENT.NUMBER_DIALED);
            }
        }
    }
    
    public void listen(){
        enabled = true;
    }
    
    public void addListener(RotaryDialListener listener) {
        if (listener != null) {
            if (listeners.add(listener)){
                LOG.log(Level.INFO, "Listener added");  
            }
        }
    }

    public void removeListener(RotaryDialListener listener) {
        if (listener != null) {
            if (listeners.remove(listener)){
                LOG.log(Level.INFO, "Listener removed");
            }
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
