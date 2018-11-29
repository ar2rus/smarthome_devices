package com.gargon.smarthome.test;

import com.gargon.smarthome.devices.dial.RotaryDial;
import com.gargon.smarthome.devices.dial.RotaryDialListener;
import java.util.Arrays;

/**
 *
 * @author gargon
 */
public class RotaryDialTest {
    
    public static void main(String[] args) throws InterruptedException{
        RotaryDial dial = new RotaryDial(0, 3, 2);
        
        dial.addListener(new RotaryDialListener() {
            @Override
            public void handleRotaryDialEvent(RotaryDial.EVENT event, Object... args) {
                System.out.print(System.currentTimeMillis() + ": " + event.toString());
                if (args.length > 0){
                    System.out.print("; " + Arrays.toString(args));
                }
                System.out.println();
            }
        });
        
        dial.listen();
        
        while (true) {
            Thread.sleep(100);
        }
    }
    
}
