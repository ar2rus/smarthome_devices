

#ifndef NEC_TX_H_
#define NEC_TX_H_

#include "nec_config.h"
#include "utils/bits.h"

// in KHz
#define NEC_CARRIER 38
#define NEC_HDR_MARK 9000
#define NEC_HDR_SPACE 4500
#define NEC_BIT_MARK  560
#define NEC_ONE_SPACE 1600
#define NEC_ZERO_SPACE  560
#define NEC_RPT_SPACE 2250

#define NEC_TOPBIT 0x80000000



#define PWMVAL ((unsigned char )(F_CPU / 2000 / NEC_CARRIER))

#define PWM_PORT B
#define PWM_PIN  1
/* phase-correct PWM with OCRA as top, no prescaling*/
/* The top value for the timer.  The modulation frequency will be SYSCLOCK / 2 / OCR2A.*/
#define PWM_SETUP { TCCR0A = _BV(WGM00); TCCR0B = _BV(WGM02) | _BV(CS00); OCR0A = PWMVAL; OCR0B = 0.3 * PWMVAL;}

#define TIMER_ENABLE_PWM     (TCCR0A |= _BV(COM0B1))
#define TIMER_DISABLE_PWM    (TCCR0A &= ~(_BV(COM0B1)))


void nec_send(char address, char command);
//void nec_send(unsigned long data, int nbits);
//void nec_send_repeat();

#endif