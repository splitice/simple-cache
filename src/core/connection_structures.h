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

struct cache_connection {
	struct cache_target target;
	unsigned int state : 16;
	unsigned int type : 8;
	//8 bytes padding
	//todo: fill with something
	int client_sock;

	//An integer for state specific data
	//that may persist over reads. Interpretation
	//is specific to the state, may be a ptr.
	//TODO: cleaner way?
	int state_data;

	//Reading from socket buffers
	struct read_buffer input;

	//Writing to socket buffers
	const char* output_buffer;
	int output_length;
	char* output_buffer_free;
};

struct cache_connection_node {
	struct cache_connection connection;
	struct cache_connection_node* next;
};

#endif