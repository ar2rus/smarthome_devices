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
import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.CommandLineParser;
import org.apache.commons.cli.DefaultParser;
import org.apache.commons.cli.HelpFormatter;
import org.apache.commons.cli.Option;
import org.apache.commons.cli.Options;

/**
 *
 * @author gargon
 */
public class RotaryDialTelephone {

    private static final int PIN_CRADLE = 14;
    private static final int PIN_DIAL_ACTIVATE = 13;
    private static final int PIN_DIAL_IMPULLSE = 12;

    private static final int MAX_DELAY_BETWEEN_DIGITS = 1500;

    private static final int BUTTON_ID = 5; //cradle (button) ID

    private static MulticastDevice device;
    private static RotaryDial dial;

    private static final String OPTION_H = "h";
    private static final String OPTION_PC = "pc";
    private static final String OPTION_PA = "pa";
    private static final String OPTION_PI = "pi";
    private static final String OPTION_MAX_DELAY = "max_delay";

    private enum ROTARY_DIAL_NUMBER_STATE {
        BEGIN(0), //начало набора номера
        DIALED(1), //номер успешно набран
        CURRENT(2), //текущий набираемый номер (набор не завершен)
        INTERRUPTED(3); //набор номер прерван или проищошла ошибка при наборе

        byte code;

        private ROTARY_DIAL_NUMBER_STATE(int code) {
            this.code = (byte) code;
        }

        public byte getCode() {
            return code;
        }
    }

    private static final Logger LOG = Logger.getLogger(RotaryDialTelephone.class.getName());

    private static void buttonResponse(int address, boolean pushed) {
        device.send(address, Smarthome.COMMAND_BUTTON_INFO,
                new byte[]{(byte) BUTTON_ID, (byte) (pushed ? 0x01 : 0x00)});
    }

    private static void sendRotaryDialNumberInfo(ROTARY_DIAL_NUMBER_STATE state, byte... args) {
        if (state != null) {

            byte[] data = new byte[(args == null ? 0 : args.length) + 1];
            data[0] = state.code;

            if (args != null) {
                switch (state) {
                    case CURRENT:
                    case DIALED:
                        System.arraycopy(args, 0, data, 1, args.length);
                        break;
                }
            }
            device.send(Smarthome.ADDRESS_BROADCAST, Smarthome.COMMAND_ROTARY_DIAL_NUMBER_INFO, data);
        }
    }

    public static void main(String[] args) {
        try {

            int pin_cradle = -1;
            int pin_activate = -1;
            int pin_impulse = -1;
            int max_delay = MAX_DELAY_BETWEEN_DIGITS;

            CommandLine a = null;
            Options options = null;

            boolean ea = true;
            try {
                CommandLineParser parser = new DefaultParser();
                options = new Options();

                Option opt = new Option(OPTION_H, false, "Show this message");
                options.addOption(opt);

                opt = new Option(OPTION_PC, true, "Cradle pin number");
                opt.setRequired(true);
                options.addOption(opt);

                opt = new Option(OPTION_PA, true, "Dial activate pin number");
                opt.setRequired(true);
                options.addOption(opt);

                opt = new Option(OPTION_PI, true, "Dial impulse pin number");
                opt.setRequired(true);
                options.addOption(opt);

                opt = new Option(OPTION_MAX_DELAY, true, "Max delay between digits dialing");
                options.addOption(opt);

                a = parser.parse(options, args);

                pin_cradle = Integer.parseInt(a.getOptionValue(OPTION_PC));
                pin_activate = Integer.parseInt(a.getOptionValue(OPTION_PA));
                pin_impulse = Integer.parseInt(a.getOptionValue(OPTION_PI));

                if (a.hasOption(OPTION_MAX_DELAY)) {
                    max_delay = Integer.parseInt(a.getOptionValue(OPTION_MAX_DELAY));
                }
                ea = false;
            } catch (Exception exp) {
                LOG.log(Level.SEVERE, "Incorrect program arguments: {0}", exp.getMessage());
            }

            if (ea || a.hasOption("h")) {
                HelpFormatter formatter = new HelpFormatter();
                formatter.printHelp("rotary-dial-telephone", options);
                return;
            }

            device = new MulticastDevice(Smarthome.ADDRESS_TELEPHONE, new MulticastDataListener() {
                @Override
                public void dataRecieved(MulticastConnection connection, InetAddress ip, MulticastDataMessage message) {
                    if (message.getCommand() == Smarthome.COMMAND_BUTTON) {
                        buttonResponse(message.getSrc(), !dial.isCradle_state());
                    }
                }
            });

            LOG.log(Level.INFO, "Device created");

            dial = new RotaryDial(pin_cradle, pin_activate, pin_impulse, max_delay);
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
                        case NUMBER_DIAL_STARTED:
                            LOG.log(Level.INFO, "Number dial started");
                            sendRotaryDialNumberInfo(ROTARY_DIAL_NUMBER_STATE.BEGIN);
                            break;
                        case NUMBER_DIAL_CURRENT:
                            if (args != null) {
                                sendRotaryDialNumberInfo(ROTARY_DIAL_NUMBER_STATE.CURRENT, args);
                            }
                            break;
                        case NUMBER_DIALED:
                            if (args != null) {
                                LOG.log(Level.INFO, "Number dialed: {0}", Arrays.toString(args));
                                sendRotaryDialNumberInfo(ROTARY_DIAL_NUMBER_STATE.DIALED, args);
                            } else {
                                LOG.log(Level.INFO, "Number dial interrupted");
                                sendRotaryDialNumberInfo(ROTARY_DIAL_NUMBER_STATE.INTERRUPTED);
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
