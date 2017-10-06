#if !defined(CONNECTION_STRUCTURES_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
#define CONNECTION_STRUCTURES_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D

#include "db_structures.h"
#include "read_buffer.h"

struct cache_target {
	struct cache_entry* entry;
	size_t position;
	size_t end_position;
	int fd;
};

struct table_target {
	struct db_table* table;
	uint32_t start;
	uint32_t limit;
};

union utarget {
	struct cache_target key;
	struct table_target table;
};

typedef enum {
	close_connection, registered_write, needs_more, continue_processing
} state_action;

struct cache_connection {
	utarget target;
	//Writing to socket buffers
	const char* output_buffer;
	int output_length;
	char* output_buffer_free;
	state_action(*handler)(int epfd, cache_connection* connection);
	uint32_t state;
	int client_sock;
	struct read_buffer input;
	
	unsigned int type : 8;
	bool writing;
};

struct cache_connection_node {
	struct cache_connection connection;
	struct cache_connection_node* next;
};

struct cache_listeners
{
	int* fds;
	uint32_t fd_count;
};

#endif