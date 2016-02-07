

#ifndef NEC_TX_H_
#define NEC_TX_H_

#include "nec_config.h"
#include "utils/bits.h"

// in KHz
#define NEC_CARRIER 38
#define NEC_HDR_MARK 9000
#define NEC_HDR_SPACE 4500
#define NEC_BIT_MARK  562
#define NEC_ONE_SPACE 1675
#define NEC_ZERO_SPACE  562
#define NEC_RPT_SPACE 2250

#define NEC_TOPBIT 0x80000000


//#define PWMVAL_A (unsigned char)(F_CPU / 2000 / NEC_CARRIER)

//используем частоту несущей 37,5
//но дополнительно можем использовать счетчик для получения rf сигнала одновременно
#define PWMVAL_A 120
#define PWMVAL_B (unsigned char)(0.3 * PWMVAL_A)

#define PWM_PORT B
#define PWM_PIN  1
/* phase-correct PWM with OCRA as top, no prescaling*/
/* The top value for the timer.  The modulation frequency will be SYSCLOCK / 2 / OCR2A.*/

//and enable compA interrupt for rf recieveng
#define PWM_SETUP {set_bit(DDRPORT(PWM_PORT), PWM_PIN);unset_bit(OUTPORT(PWM_PORT), PWM_PIN); TCCR0A = _BV(WGM00); TCCR0B = _BV(WGM02) | _BV(CS00); OCR0A = PWMVAL_A; OCR0B = PWMVAL_B; TIMSK0 = 0; set_bit2(TIMSK0, OCIE0A, TOIE0);}

#define TIMER_ENABLE_PWM     (TCCR0A |= _BV(COM0B1))
#define TIMER_DISABLE_PWM    (TCCR0A &= ~(_BV(COM0B1)))

#define NEC_HDR_MARK_T (unsigned int)(NEC_HDR_MARK * ((double)F_CPU / 1000000UL) / (PWMVAL_A))
#define NEC_HDR_SPACE_T (unsigned int)(NEC_HDR_SPACE * ((double)F_CPU / 1000000UL) / (PWMVAL_A))
#define NEC_BIT_MARK_T  (unsigned int)(NEC_BIT_MARK * ((double)F_CPU / 1000000UL) / (PWMVAL_A))
#define NEC_ONE_SPACE_T (unsigned int)(NEC_ONE_SPACE * ((double)F_CPU / 1000000UL) / (PWMVAL_A))
#define NEC_ZERO_SPACE_T (unsigned int)(NEC_ZERO_SPACE * ((double)F_CPU / 1000000UL) / (PWMVAL_A))
#define NEC_RPT_SPACE_T (unsigned int)(NEC_RPT_SPACE * ((double)F_CPU / 1000000UL) / (PWMVAL_A))



//void nec_init();
//void nec_send(char address, char command);

//void nec_send(unsigned long data, int nbits);
//void nec_send_repeat();

#endif