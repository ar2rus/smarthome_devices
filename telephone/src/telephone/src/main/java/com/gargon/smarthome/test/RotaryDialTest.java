/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package com.gargon.smarthome.test;

import com.gargon.smarthome.devices.dial.RotaryDial;
import com.gargon.smarthome.devices.dial.RotaryDialListener;
import java.sql.Array;
import java.util.Arrays;

/**
 *
 * @author gargon
 */
public class RotaryDialTest {
    
    public static void main(String[] args) throws InterruptedException{
        RotaryDial dial = new RotaryDial();
        
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
