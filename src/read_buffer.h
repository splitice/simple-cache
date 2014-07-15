#define BUFFER_SIZE 4096

struct read_buffer {
    char buffer[BUFFER_SIZE];
    int read_position;
    int write_position;
}

void rbuf_copyn(char* dest, int n);
int rbuf_read_remaining(read_buffer* buffer);
int rbuf_read_to_end(read_buffer* buffer);
int rbuf_write_remaining(read_buffer* buffer);
int rbuf_write_to_end(read_buffer* buffer);