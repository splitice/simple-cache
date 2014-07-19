#include <stdbool.h>

#define BUFFER_SIZE 4096

/* A circular read buffer */
struct read_buffer {
    char buffer[BUFFER_SIZE];
    int read_position;
    int write_position;
};

/*
Initialize the read and write position of a read_buffer
*/
void rbuf_init(struct read_buffer* buf);

/*
Copy n bytes from the buffer,
returns the number of bytes copied.
*/
int rbuf_copyn(struct read_buffer* buffer, char* dest, int n);

/*
Compare n bytes from the buffer from the read position,
returns the comparison result
*/
int rbuf_cmpn(struct read_buffer* buffer, const char* with, int n);

/*
Extract integer from character stream (ASCII)
Only process up to a maximum number of bytes (-1 for entire stream)
*/
bool rbuf_strntol(struct read_buffer* buffer, int* output, int max);

/*
Get the number of bytes remaining to read
*/
int rbuf_read_remaining(struct read_buffer* buffer);

/*
Get the number of contiguous bytes remaining to be read
until the end (highest index)
*/
int rbuf_read_to_end(struct read_buffer* buffer);

/*
Get the number of bytes that can be written until maximum
capacity.
*/
int rbuf_write_remaining(struct read_buffer* buffer);

/*
Get the number of contiguous bytes that can be written
*/
int rbuf_write_to_end(struct read_buffer* buffer);

/*
Get a pointer to the buffer at the current read position
*/
#define RBUF_READ(x) x.buffer + x.read_position

/*
Get a pointer to the buffer at the current read position plus an offset
*/
#define RBUF_READIDX(x, i) x.buffer + ((x.read_position + i) % BUFFER_SIZE)

/*
Get a pointer to the buffer at the start (idx:0)
*/
#define RBUF_START(x) x.buffer

/*
Get a pointer to the buffer at the current write offset
*/
#define RBUF_WRITE(x) x.buffer + x.write_position

/*
Move the read offset
*/
#define RBUF_READMOVE(x, by) x.read_position = ((x.read_position + by) % BUFFER_SIZE)

/*
Move the write offset
*/
#define RBUF_WRITEMOVE(x, by) x.write_position = ((x.write_position + by) % BUFFER_SIZE)

/*
Get a pointer to the buffer at the current read position
*/
#define RBUF_READPTR(x) x->buffer + x->read_position

/*
Get a pointer to the buffer at the current read position plus an offset
*/
#define RBUF_READIDXPTR(x, i) x->buffer + ((x->read_position + i) % BUFFER_SIZE)

/*
Get a pointer to the buffer at the start (idx:0)
*/
#define RBUF_STARTPTR(x) x->buffer

/*
Get a pointer to the buffer at the current write offset
*/
#define RBUF_WRITEPTR(x) x->buffer + x->write_position

/*
Move the read offset
*/
#define RBUF_READMOVEPTR(x, by)  x->read_position = ((x->read_position + by) % BUFFER_SIZE)

/*
Move the write offset
*/
#define RBUF_WRITEMOVEPTR(x, by) x->write_position = ((x->write_position + by) % BUFFER_SIZE)

/*
Helper to Iterate over circular buffer
*/
#define RBUF_ITERATE(rb,n,buffer,end,inner) do { \
	end = rbuf_read_to_end(&rb); \
	n = 0; \
	if (end != 0){ \
		buffer = RBUF_READ(rb); \
		for (; n < end; n++){ \
			inner; \
			buffer++; \
		} \
		end = rbuf_read_remaining(&rb) - end; \
		for (int i = 0; i < end; i++) { \
			inner; \
			buffer++; \
			n++; \
		} \
	} \
} while (0);
