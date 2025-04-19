#ifndef KitchenLight_h
#define KitchenLight_h

#define CLUNET_DEVICE_ID 0x89
#define CLUNET_DEVICE_NAME "KitchenLightNew"

#define TIMEZONE TZ_Europe_Samara

#define RELAY_0_ID 1
#define BUTTON_ID 3

#define BUTTON_PIN 16
#define LIGHT_PIN 14


#define PWM_RANGE 255
#define PWM_FREQUENCY 100

#define DELAY_BEFORE_TOGGLE 25
#define DELAY_BEFORE_PWM 500

#define PWM_DOWN_UP_CYCLE_TIME 4000
#define PWM_DOWN_UP_CYCLE_TIME_2 (int)(PWM_DOWN_UP_CYCLE_TIME/2)

#endif
