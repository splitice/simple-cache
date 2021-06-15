#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>

struct scache_bind
{
	int af;
	char addr[sizeof(struct sockaddr_un)];//Largest IP address format supported
	int port;
};

struct scache_binds {
	scache_bind* binds;
	int num;
};

struct scache_settings {
    uint64_t max_size;
    char* db_file_path;
    float db_lru_clear;
	char* pidfile;
	bool daemon_mode;
	bool daemon_output;
	struct scache_binds bind_cache;
	struct scache_binds bind_monitor;
};
extern struct scache_settings settings;
void settings_parse_arguments(int argc, char** argv);
void settings_cleanup();