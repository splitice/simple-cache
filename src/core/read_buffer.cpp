#include <assert.h>
#include <string.h>
#include "read_buffer.h"
#include "debug.h"

int rbuf_copyn(struct read_buffer* buffer, char* dest, int n) {
	assert(n <= BUFFER_SIZE);
	int to_end = rbuf_read_to_end(buffer);

	if (n == 0){
		//No bytes to be read
		return 0;
	}

	//Read the maximum we can / want
	int to_read = n;
	if (n > to_end){
		to_read = to_end;
	}

	//Copy this ammount
	memcpy(dest, RBUF_READPTR(buffer), to_read);
	n -= to_read;
	dest += to_read;

	//Do we need more?
	if (n == 0){
		return to_read;
	}

	//Second memcpy, read the roll over
	to_end = rbuf_read_remaining(buffer) - to_end;
	if (to_end != 0){
		//Dont read more than needed
		if (to_end > n){
			to_end = n;
		}

		memcpy(dest, RBUF_STARTPTR(buffer), to_end);
	}

	//We have copied the sum of the two
	return to_read + to_end;
}

int rbuf_cmpn(struct read_buffer* buffer, const char* with, int n) {
	assert(n <= BUFFER_SIZE);
	int to_end = rbuf_read_to_end(buffer);

	if (n == 0){
		//No bytes to be read
		return 0;
	}

	//Read the maximum we can / want
	int to_read = n;
	if (n > to_end){
		to_read = to_end;
	}

	//Copy this ammount
	int result = strncmp(with, RBUF_READPTR(buffer), to_read);
	n -= to_read;
	with += to_read;

	//Do we need more?
	//Only continue if we are equal up to here
	if (n == 0 || result != 0){
		return result;
	}

	//TODO: how to handle insufficient bytes?
	//Second memcpy, read the roll over
	to_end = rbuf_read_remaining(buffer) - to_end;
	if (to_end != 0){
		//Dont read more than needed
		if (to_end > n){
			to_end = n;
		}

		return strncmp(with, RBUF_STARTPTR(buffer), to_end);
	}

	//Shouldnt happen
	return result;
}

bool rbuf_strntol(struct read_buffer* buffer, int* output, int max)
{
	int result = 0;
	int n = rbuf_read_to_end(buffer);
	if (max != -1 && max < n){
		n = max;
		max -= n;
	}

	char* buf = RBUF_READPTR(buffer);

	//Handle forward
	while (n--)
	{
		result *= 10;
		int number = (*buf++) - '0';

		if (number > 9 || number < 0){
			DEBUG("Expected number got char %c\n", (unsigned char)(*(buf - 1)));
			return false;
		}
		result += number;
	}

	if (max != 0){
		//How many after roll over?
		n = rbuf_read_remaining(buffer) - n;
		buf = RBUF_STARTPTR(buffer);

		//Handle rollover
		while (n--)
		{
			result *= 10;
			int n = (*buf++) - '0';

			if (n > 9 || n < 0){
				DEBUG("Expected number got char %c\n", (unsigned char)(*(buf - 1)));
				return false;
			}
			result += n;
		}
	}

	*output = result;
	return true;
}

int rbuf_read_remaining(struct read_buffer* buffer) {
	return RBUF_READLENPTR(buffer);
}
int rbuf_read_to_end(struct read_buffer* buffer) {
	/*uint16_t t = BUFFER_SIZE - (buffer->read_position % BUFFER_SIZE);
	uint16_t y = buffer->write_position - buffer->read_position;
	if (t < y){
		return t;
	}
	return y;*/

	uint16_t to_end = (buffer->read_position & (BUFFER_SIZE - 1));
	if (to_end <= (buffer->write_position & (BUFFER_SIZE - 1))){
		return buffer->write_position - buffer->read_position;
	}
	else{
		return BUFFER_SIZE - to_end;
	}
}
int rbuf_write_remaining(struct read_buffer* buffer) {
	return RBUF_WRITELENPTR(buffer);
}
int rbuf_write_to_end(struct read_buffer* buffer) {
	uint16_t to_end = (buffer->write_position & (BUFFER_SIZE - 1));
	if (to_end <= (buffer->read_position & (BUFFER_SIZE - 1))){
		return (BUFFER_SIZE - buffer->write_position) + buffer->read_position;
	}
	else{
		return BUFFER_SIZE - to_end;
	}
}

void rbuf_init(struct read_buffer* buf){
	buf->read_position = 0;
	buf->write_position = 0;
#ifdef DEBUG_BUILD
	buf->write_remaining = 0;
#endif
}

void rb_debug_read_check(struct read_buffer* buffer, int by){
#ifdef DEBUG_BUILD
	buffer->write_remaining -= by;
	assert(buffer->write_remaining >= 0);
#endif
}

void rb_debug_write_incr(struct read_buffer* buffer, int by){
#ifdef DEBUG_BUILD
	buffer->write_remaining += by;
#endif
}