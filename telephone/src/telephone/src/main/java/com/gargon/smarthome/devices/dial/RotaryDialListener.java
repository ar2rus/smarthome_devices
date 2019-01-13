package com.gargon.smarthome.devices.dial;

/**
 *
 * @author gargon
 */
public interface RotaryDialListener {
    
    public void handleRotaryDialEvent(RotaryDial.EVENT event, byte... args);
    
}
