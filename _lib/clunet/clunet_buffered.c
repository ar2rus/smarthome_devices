/*
 * clunet_buffered.c
 *
 * Created: 14.01.2016 14:01:11
 *  Author: gargon
 */ 

#include "clunet_buffered.h"

FIFO(CLUNET_BUFFERED_BUFFER_SIZE) fifo;

void clunet_buffered_init(){
	FIFO_FLUSH(fifo);
}

char clunet_buffered_push(unsigned char src_address, unsigned char dst_address,
			unsigned char command, char* data, unsigned char size){
	if (!FIFO_IS_FULL(fifo)){
		if (size <= CLUNET_BUFFERED_DATA_MAX_LENGTH){
			clunet_msg* msg = FIFO_PUSH(fifo);
			
			msg->src_address = src_address;
			msg->dst_address = dst_address;
			msg->command = command;
			msg->size = size;
			memcpy(&(msg->data), data, size);
			
			return 1;
		}
	}
	return 0;
}

char clunet_buffered_is_empty(){
	return FIFO_IS_EMPTY(fifo);
}

clunet_msg* clunet_buffered_pop(){
	clunet_msg* r = NULL;
	if (!FIFO_IS_EMPTY(fifo)){
		r = &FIFO_FRONT(fifo);
		FIFO_POP(fifo);
	}
	return r;
}