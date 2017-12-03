#ifndef NIXIE_h
#define NIXIE_h

#include <stdint.h>

#define SPI_PIN_SS 15

//ms
#define NIXIE_UPDATE_PERIOD  2

#define DIGITS_COUNT 6

#define _BV(bit) (1 << (bit))
#define _TB(v, bit) (v & _BV(bit))


#define digit_code(digit, enable, point) (((!enable & 0x01)<<0x07) | ((point & 0x01)<<0x06) | (digit & 0x0F))
#define digit_off (digit_code(0,0,0))

#define digit_value(code) (code & 0x0F)
#define digit_enable(code) (!_TB(code, 7))
#define digit_point(code) (_TB(code, 6))

#define digit_code_group_h(d) ((_TB(d, 0) ? _BV(4) : 0) | (_TB(d, 1) ? _BV(2) : 0) | (_TB(d, 2) ? _BV(1) : 0) | (_TB(d, 3) ? _BV(3) : 0))
#define digit_code_group_l(d) ((_TB(d, 0) ? _BV(7) : 0) | (_TB(d, 1) ? _BV(5) : 0) | (_TB(d, 2) ? _BV(4) : 0) | (_TB(d, 3) ? _BV(6) : 0))

#define digit_group_h(d, r0, r1, r2)(digit_code_group_h(digit_value(d)) | (digit_point(d) ? _BV(7) : 0) |\
  (digit_enable(d) && r0 ? _BV(0) : 0) | (digit_enable(d) && r1 ? _BV(5) : 0) | (digit_enable(d) && r2 ? _BV(6) : 0))
#define digit_group_l(d, r0, r1, r2)(digit_code_group_l(digit_value(d)) | (digit_point(d) ? _BV(3) : 0) |\
  (digit_enable(d) && r0 ? _BV(2) : 0) | (digit_enable(d) && r1 ? _BV(1) : 0) | (digit_enable(d) && r2 ? _BV(0) : 0))

void nixie_init();

void nixie_set(char d0, char d1, char d2, char d3, char d4, char d5);

void nixie_set(uint32_t v, uint8_t pos_1);

void nixie_set(float v, uint8_t pos_1, int num_frac);

void nixie_clear();

#endif
