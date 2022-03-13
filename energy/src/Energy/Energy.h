#ifndef Energy_h
#define Energy_h

#define CLUNET_DEVICE_ID 0x86
#define CLUNET_DEVICE_NAME "EnergyMeter"

#define TIMEZONE TZ_Europe_Samara

#define PZEM_UPDATE_TIMEOUT 250

#define RESET_ENERGY_BUTTON_PIN 0
#define RESET_ENERGY_BUTTON_TIMEOUT 10000

#define PZEM004_NO_SWSERIAL

typedef struct {
    float voltage;
    float current;
    float power;
    float energy;
    float frequency;
    float pf;
} pzemValues;

#endif
