#include "Leds.h"


Leds::Leds(){
	FastLED.addLeds<WS2812B, LED_PIN, GRB>(_leds, _num).setCorrection(TypicalPixelString);
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

uint8_t Leds::get(CRGB* leds){
  memcpy(leds, &_leds, _num * sizeof(CRGB));
  return _num;
}

String Leds::info(){
    String value = "";
    for (int i=0; i<_num; i++){
      String r = String(_leds[i].r, HEX);
      if (r.length() < 2){
        r = "0" + r;
      }
      String g = String(_leds[i].g, HEX);
      if (g.length() < 2){
        g = "0" + g;
      }
      String b = String(_leds[i].b, HEX);
      if (b.length() < 2){
        b = "0" + b;
      }
      value += (r + g + b) + "\r\n";
    }
    return value;
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
 backlight();
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
