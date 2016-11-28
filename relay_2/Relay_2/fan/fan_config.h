/*
 * fan_config.h
 *
 * Created: 21.09.2015 17:12:47
 *  Author: gargon
 */ 


#ifndef FAN_CONFIG_H_

#include <avr/io.h>

//������������ ������ ������� ��������� (� ��������)
#define FAN_HUMIDITY_CHECK_TIME 30

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

//�������� ���������, ��� ������� �� ������������ 
//�������������� ��������� �����������
#define HUMIDITY_MIN_ABS 50

//����� � EEPROM ��� �������� ������. ����������:
//���.�����
#define EEPROM_ADDRESS_FAN_CONFIG_MODE 0x00

#define FAN_CONFIG_H_


#endif /* FAN_CONFIG_H_ */