#define BUFFER_SIZE 4096

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
#define RBUF_READPTR(x) x->buffer + x->read_position

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
#define RBUF_READMOVE(x, by) x->read_position += by

/*
Move the write offset
*/
#define RBUF_WRITEMOVE(x, by) x->write_position += by