#include <stdint.h>
#include <sys/socket.h>
#include <netdb.h>

struct scache_bind
{
	int af;
	char addr[sizeof(struct in6_addr)];//Largest IP address format supported
	int port;
};

struct scache_settings {
    uint64_t max_size;
    char* db_file_path;
    float db_lru_clear;
	scache_bind* bind;
	int bind_num;
	char* pidfile;
	bool daemon_mode;
	bool daemon_output;
};
extern struct scache_settings settings;
void settings_parse_arguments(int argc, char** argv);
void settings_cleanup();