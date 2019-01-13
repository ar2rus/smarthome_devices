package com.gargon.smarthome.devices;

import com.gargon.smarthome.Smarthome;
import com.gargon.smarthome.multicast.MulticastConnection;
import com.gargon.smarthome.multicast.MulticastDataListener;
import com.gargon.smarthome.multicast.MulticastDevice;
import com.gargon.smarthome.multicast.messages.MulticastDataMessage;
import java.net.InetAddress;
import java.util.Arrays;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 *
 * @author gargon
 */
public class RotaryDialTelephone {

    private static final int PIN_CRADLE = 14;
    private static final int PIN_DIAL_ACTIVATE = 13;
    private static final int PIN_DIAL_IMPULLSE = 12;

    private static final int BUTTON_ID = 5; //cradle (button) ID

    private static MulticastDevice device;
    private static RotaryDial dial;

    private static final Logger LOG = Logger.getLogger(RotaryDialTelephone.class.getName());

    private static void buttonResponse(int address, boolean pushed) {
        device.send(address, Smarthome.COMMAND_BUTTON_INFO,
                new byte[]{(byte) BUTTON_ID, (byte) (pushed ? 0x01 : 0x00)});
    }

    public static void main(String[] args) {
        try {
            device = new MulticastDevice(Smarthome.ADDRESS_TELEPHONE, new MulticastDataListener() {
                @Override
                public void dataRecieved(MulticastConnection connection, InetAddress ip, MulticastDataMessage message) {
                    if (message.getCommand() == Smarthome.COMMAND_BUTTON) {
                        buttonResponse(message.getSrc(), !dial.isCradle_state());
                    }
                }
            });

            LOG.log(Level.INFO, "Device created");

            dial = new RotaryDial(PIN_CRADLE, PIN_DIAL_ACTIVATE, PIN_DIAL_IMPULLSE);
            dial.addListener(new RotaryDialListener() {
                @Override
                public void handleRotaryDialEvent(RotaryDial.EVENT event, byte... args) {
                    switch (event) {
                        case PICKED_UP:
                            LOG.log(Level.INFO, "Picked up");
                            buttonResponse(Smarthome.ADDRESS_BROADCAST, false);
                            break;
                        case PUT_DOWN:
                            LOG.log(Level.INFO, "Put down");
                            buttonResponse(Smarthome.ADDRESS_BROADCAST, true);
                            break;
                        case NUMBER_DIALED:
                            if (args != null) {
                                LOG.log(Level.INFO, "Number dialed: {0}", Arrays.toString(args));
                                device.send(Smarthome.ADDRESS_BROADCAST, Smarthome.COMMAND_ROTARY_DIAL_NUMBER_INFO, args);
                            }
                            break;
                    }
                }
            });

            dial.listen();
            LOG.log(Level.INFO, "Rotary dial started to listen");

            Runtime.getRuntime().addShutdownHook(new Thread() {
                @Override
                public void run() {
                    if (device != null) {
                        device.close();
                    }

                    dial = null;
                    LOG.log(Level.INFO, "Application closed");
                }
            });

        } catch (Exception e) {

            if (device != null) {
                device.close();
            }

            dial = null;
            LOG.log(Level.SEVERE, "initialization error", e);

        }
    }

}