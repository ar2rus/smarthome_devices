/*
 * clunet_buffered.h
 *
 * Created: 14.01.2016 14:01:39
 *  Author: gargon
 */ 



#ifndef CLUNET_BUFFERED_H_
#define CLUNET_BUFFERED_H_

#include <string.h>
#include "clunet_config.h"

typedef struct {
	unsigned char src_address;
	unsigned char dst_address;
	unsigned char command;
	unsigned char size;
	char data[CLUNET_BUFFERED_DATA_MAX_LENGTH];
} clunet_msg;

//������ ������ ���� �������� ������: 4, 8, 16, 32...128
#define FIFO( size )\
	struct {\
		clunet_msg buf[size];\
		unsigned char tail;\
		unsigned char head;\
	}

//���������� ��������� � �������
#define FIFO_COUNT(fifo)     (fifo.head-fifo.tail)

//������ fifo
#define FIFO_SIZE(fifo)      ( sizeof(fifo.buf)/sizeof(fifo.buf[0]) )

//fifo ���������?
#define FIFO_IS_FULL(fifo)   (FIFO_COUNT(fifo)==FIFO_SIZE(fifo))

//fifo �����?
#define FIFO_IS_EMPTY(fifo)  (fifo.tail==fifo.head)

//���������� ���������� ����� � fifo
#define FIFO_SPACE(fifo)     (FIFO_SIZE(fifo)-FIFO_COUNT(fifo))

//��������� ������� � fifo
#define FIFO_PUSH(fifo) (\
	&fifo.buf[fifo.head++ & (FIFO_SIZE(fifo)-1)]\
)

//����� ������ ������� �� fifo
#define FIFO_FRONT(fifo) (fifo.buf[fifo.tail & (FIFO_SIZE(fifo)-1)])

//��������� ���������� ��������� � �������
#define FIFO_POP(fifo)   {\
	fifo.tail++; \
}

//�������� fifo
#define FIFO_FLUSH(fifo) {\
	fifo.tail=0;\
	fifo.head=0;\
}


void clunet_buffered_init();
char clunet_buffered_push(unsigned char src_address, unsigned char dst_address,
	unsigned char command, char* data, unsigned char size);
char clunet_buffered_is_empty();
clunet_msg* clunet_buffered_pop();

#endif /* CLUNET_BUFFERED_H_ */