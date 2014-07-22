#if !defined(CONNECTION_STRUCTURES_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
#define CONNECTION_STRUCTURES_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D

#include "db_structures.h"
#include "read_buffer.h"

struct cache_target {
	struct cache_entry* entry;
	int position;
	int end_position;
	int fd;
};

struct table_target {
	struct db_table* table;
};

union utarget {
	struct cache_target key;
	struct table_target table;
};

struct cache_connection {
	utarget target;
	//todo: fill with something
	int client_sock;

	//Reading from socket buffers
	struct read_buffer input;

	//Writing to socket buffers
	const char* output_buffer;
	int output_length;
	char* output_buffer_free;
	unsigned int state : 16;
	unsigned int type : 8;
	//8 bytes padding
};

struct cache_connection_node {
	struct cache_connection connection;
	struct cache_connection_node* next;
};

#endif