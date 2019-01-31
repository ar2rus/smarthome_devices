#ifndef LEDS_h
#define LEDS_h

#define FASTLED_ALLOW_INTERRUPTS 0
//#define FASTLED_INTERRUPT_RETRY_COUNT 10
#include <FastLED.h>

#include <functional>

#define LED_PIN 5
#define LED_COUNT 6

typedef std::function<void(CRGB* leds, uint8_t leds_num, uint8_t* brightness)> LedsSetupHandlerFunction;


class Leds{
private:
  const uint8_t _num = LED_COUNT;
  
	CRGB _leds[LED_COUNT];
	
	uint8_t _brightness = 40;
	
	bool _backlight_on = false;
	CRGB _backlight_color = CRGB::White;
	
	void backlight_on(bool on);
public:
  Leds();

  uint8_t get(CRGB* leds);
  String info();
	
	void backlight();		//switches to backlight (if enabled)
	void set(CRGB color);	//shows all leds with the same color
	void set(LedsSetupHandlerFunction setupFunction);	//show custom colors
	void rainbow();			//shows rainbow
	
	void backlight_on();
	void backlight_off();
	void backlight_toggle();

	void setBrightness(uint8_t brightness);
	void setBacklightColor(CRGB color);
};

#endif
