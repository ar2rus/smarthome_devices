#include "Leds.h"


Leds::Leds(const uint8_t pin, uint8_t num_leds){
	_num = num_leds;
	
	_leds = new CRGB[_num];
	FastLED.addLeds<WS2812B, 5, GRB>(_leds, num_leds).setCorrection(TypicalPixelString);
	
	backlight();
}

void Leds::set(LedsSetupHandlerFunction setupFunction){
	CRGB leds[_num];
	memcpy(&leds, &_leds, _num * sizeof(CRGB));
	
	uint8_t brightness = _brightness;
	
    if (setupFunction){
      setupFunction(leds, _num, &brightness);
    }
	
	bool changed = false;
	for (int i = 0; i < _num; i++) {
		if (_leds[i] != leds[i]) {
			changed = true;
			_leds[i] = leds[i];
		}
	}
	
	changed |= (brightness != _brightness);

	if (changed) {
		FastLED.setBrightness(brightness);
		FastLED.show();
	}
}

void Leds::set(CRGB color){
	set([&color](CRGB* leds, uint8_t leds_num, uint8_t* brightness){
		fill_solid(leds, leds_num, color);
	});
}

void Leds::backlight(){
  set(_backlight_on ? _backlight_color : CRGB::Black);
}

#define BPM       60
#define DIMMEST   0
#define BRIGHTEST 255

static uint16_t hue16 = 0;
		  
void Leds::rainbow(){
	  set([](CRGB* leds, uint8_t leds_num, uint8_t* brightness){
		  hue16 += 9;
		  fill_rainbow( leds, leds_num, hue16 / 256, 0);
		  *brightness = beatsin8( BPM, DIMMEST, BRIGHTEST);
	  });
}

void Leds::backlight_on(bool on){
	_backlight_on = on;
}

void Leds::backlight_on(){
	backlight_on(true);
}

void Leds::backlight_off(){
	backlight_on(false);
}

void Leds::backlight_toggle(){
	backlight_on(!_backlight_on);
}

void Leds::setBrightness(uint8_t brightness){
	_brightness = brightness;
}

void Leds::setBacklightColor(CRGB color){
	_backlight_color = color;
}
