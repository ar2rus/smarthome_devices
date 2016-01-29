/*
 * rx.c
 *
 * Created: 28.10.2014 22:02:34
 *  Author: gargon
 */ 

#include "rx.h"

uint8_t status;
uint16_t tick;

uint8_t ready;
uint32_t value;


uint8_t necCheckSignal(){
	if (!test_bit(status, NEC_STATUS_START_SCAN)){								//���� ��� �� ������ ������
		if (!test_bit(INPORT(NEC_PORT), NEC_PIN)){								//���� ������� �������
			set(status, _BV(NEC_STATUS_START_SCAN)|_BV(NEC_STATUS_PIN));
			tick = 0;
			return 1;
		}else{
			if (++tick > NEC_NUM_TICKS_WAIT_REPEAT){							//������� ��� ������� �� ����� 50 ��
				value = 0;
			}
		}
	}
	return 0;
}


uint8_t necReadSignal(){
	uint8_t r = 1;
	tick++;
	if (bit(status, NEC_STATUS_PIN) ^ bit(INPORT(NEC_PORT), NEC_PIN)){						//���� ����� �������� �� IR
		
		flip_bit(status, NEC_STATUS_PIN);
		if (!test_bit(status, NEC_STATUS_PIN)){
			if (tick >= NEC_NUM_TICKS_START_COND_1 && tick < NEC_NUM_TICKS_START_COND_2){	//��������� ��������� �������
					set_bit(status, NEC_STATUS_START_COND);
					value = 0;
					ready = 0;
			}else if (tick >= NEC_NUM_TICKS_REPEAT_COND_1 && tick < NEC_NUM_TICKS_REPEAT_COND_2){
					ready = 1;
					r = 0;
			}else if (test_bit(status, NEC_STATUS_START_COND)){
				if (tick >= NEC_NUM_TICKS_LOG1_1 && tick < NEC_NUM_TICKS_LOG1_2){			//������� 1
					++status;																//++b_cnt;
					value = (value<<1) | 1;
				}else if (tick >= NEC_NUM_TICKS_LOG0_1 && tick < NEC_NUM_TICKS_LOG0_2){		//������� 0
					++status;																//++b_cnt;
					value = (value<<1);
				}
				
				if (!test_bit(status, NEC_STATUS_START_COND)){								//���� ������� ��� 4 ����� - ��������� ��������� NEC_STATUS_START_COND
					uint8_t a =  value;														//checksum
					uint8_t b = (value >> 8);
					if ((a^b) == 0xFF){
						ready = 1;
						r = 0;
					}
				}
			}
			tick = 0;
		}
	}else if (tick > NEC_NUM_TICKS_TIMEOUT){												//���� ��������� ����� 2500 ����� (2500 * 50 ��� = 125 ��) - ������ 110
		r = 0;
	}
	
	if (!r){
		status = 0;
	}
	
	return r;
}

uint8_t necValue(uint8_t *address, uint8_t *command){
	if (ready){
		
		*address = value >> 24;
		*command = value >> 8;
	
		ready = 0;
		return 1;
	}
	return 0;
}

void necResetValue(){
	value = 0;
}