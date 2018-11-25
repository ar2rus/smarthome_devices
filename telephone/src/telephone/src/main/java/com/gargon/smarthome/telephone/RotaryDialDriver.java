package com.gargon.smarthome.telephone;

import com.pi4j.io.gpio.GpioController;
import com.pi4j.io.gpio.GpioFactory;
import com.pi4j.io.gpio.GpioPinDigitalInput;
import com.pi4j.io.gpio.Pin;
import com.pi4j.io.gpio.PinPullResistance;
import com.pi4j.io.gpio.RaspiPin;
import com.pi4j.io.gpio.event.GpioPinDigitalStateChangeEvent;
import com.pi4j.io.gpio.event.GpioPinListenerDigital;

/**
 *
 * @author gargon
 */
public class RotaryDialDriver {
    
    public static final Pin DISK_ACTIVATE_PIN = RaspiPin.GPIO_02;
    public static final Pin DISK_IMPULSE_PIN = RaspiPin.GPIO_03;
    
    public static final Pin CRADLE_PIN = RaspiPin.GPIO_04;
    
    public static void main(String[] args) throws InterruptedException{
        
        final GpioController gpio = GpioFactory.getInstance();
        final GpioPinDigitalInput diskActivate = gpio.provisionDigitalInputPin(DISK_ACTIVATE_PIN, PinPullResistance.PULL_UP);
        diskActivate.setShutdownOptions(true);
         
        System.out.println("Listen for pin changes");
        
        diskActivate.addListener(new GpioPinListenerDigital() {
            long prev_t = 0;
            
            @Override
            public void handleGpioPinDigitalStateChangeEvent(GpioPinDigitalStateChangeEvent event) {
                long t = System.currentTimeMillis();
                System.out.println((t-prev_t) + ": " + event.getPin() + " = " + event.getState());
                prev_t = t;
            }
        });
        
         while(true) {
            Thread.sleep(500);
        }
        
    }
    
}
