/*
 * fan_config.h
 *
 * Created: 21.09.2015 17:12:47
 *  Author: gargon
 */ 


#ifndef FAN_CONFIG_H_

#include <avr/io.h>

//������������ ������ ������� ��������� (� ��������)
#define FAN_HUMIDITY_CHECK_TIME 15

#if FAN_HUMIDITY_CHECK_TIME < 5
#  error Humidity check time too frequent, decrease it
#endif

#if FAN_HUMIDITY_CHECK_TIME > 60
#  error Humidity check time too rare, increase it
#endif

//������������ ����� ������ ����������� (� ��������), 30 ���
#define FAN_TIMEOUT 1800

//������ �������� ������� ��������� (���-�� ��������� � ������)
#define HUMIDITY_HISTORY_COUNT 60 / FAN_HUMIDITY_CHECK_TIME

//���������� ������� �������� ������ ��������� ������������ ��������, ������������ �� �������,
//����� �������� ��������� ��������� �����������
#define HUMIDITY_DELTA_PLUS 3
//�������� ���������, ��� ������� ��������� ��������� �����������
//��� ������������ �������� �� �����
#define HUMIDITY_MAX_ABS 85


/* fan main timer controls*/
#define FAN_TIMER_PRESCALER 256
#define FAN_TIMER_NUM_TICKS (unsigned int)(1 * F_CPU / FAN_TIMER_PRESCALER)	/*1 second main loop*/
#define FAN_TIMER_INIT {TCCR1B = 0; TCNT1 = 0; OCR1A = FAN_TIMER_NUM_TICKS; set_bit(TCCR1B, CS12); unset_bit2(TCCR1B, CS11, CS10); /*256x prescaler*/}

#define FAN_TIMER_REG TCNT1

#define ENABLE_FAN_TIMER_CMP_A set_bit(TIMSK, OCIE1A)
#define DISABLE_FAN_TIMER_CMP_A unset_bit(TIMSK, OCIE1A)

#define FAN_TIMER_COMP_VECTOR TIMER1_COMPA_vect

#define FAN_CONFIG_H_





#endif /* FAN_CONFIG_H_ */