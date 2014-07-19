#include <stdint.h>
#include <sys/socket.h>
#include <netdb.h>

struct scache_settings {
    uint64_t max_size;
    char* db_file_path;
    float db_lru_clear;

	int bind_af;
	char bind_addr[sizeof(struct in6_addr)];//Largest IP address format supported
    int bind_port;
};
extern struct scache_settings settings;
void parse_arguments(int argc, char** argv);