#define BUFFER_SIZE 4096

struct read_buffer {
    char buffer[BUFFER_SIZE];
    int read_position;
    int write_position;
};

/*
Copy n bytes from the buffer
*/
void rbuf_copyn(struct read_buffer* buffer, char* dest, int n);

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