/*
 * bits.h
 *
 * Created: 22.10.2014 20:18:20
 *  Author: gargon
 */ 


#ifndef BITS_H_
#define BITS_H_


#define set(reg,value) reg |= (value)
#define unset(reg,value) reg &= ~(value)
#define set_bit(reg,bit) reg |= (_BV(bit))
#define set_bit2(reg,bit1,bit2) reg |= (_BV(bit1) | _BV(bit2))
#define set_bit3(reg,bit1,bit2,bit3) reg |= (_BV(bit1) | _BV(bit2) | _BV(bit3))
#define set_bit4(reg,bit1,bit2,bit3,bit4) reg |= (_BV(bit1) | _BV(bit2) | _BV(bit3) | _BV(bit4))
#define unset_bit(reg,bit) reg &= ~(_BV(bit))
#define unset_bit2(reg,bit1,bit2) reg &= ~(_BV(bit1) | _BV(bit2))
#define unset_bit3(reg,bit1,bit2,bit3) reg &= ~(_BV(bit1) | _BV(bit2) | _BV(bit3))
#define unset_bit4(reg,bit1,bit2,bit3,bit4) reg &= ~(_BV(bit1) | _BV(bit2) | _BV(bit3) | _BV(bit4))

#define flip_bit(reg, bit) reg ^= _BV(bit);
#define test_bit(reg, bit) (reg & _BV(bit))
#define bit(reg, bit) (test_bit(reg, bit)>>bit)


#endif /* BITS_H_ */