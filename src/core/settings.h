#include <stdint.h>
#include <sys/socket.h>

struct scache_settings {
    uint64_t max_size;
    char* db_file_path;
    float db_lru_clear;

    struct sockaddr bind_addr;
    int bind_port;
};
extern struct scache_settings settings;