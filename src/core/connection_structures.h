#if !defined(CONNECTION_STRUCTURES_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
#define CONNECTION_STRUCTURES_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D

#include "db_structures.h"
#include "read_buffer.h"

typedef struct {
	cache_entry* entry;
	int position;
	int end_position;
	int fd;
} cache_target;

typedef struct {
	cache_target target;
	int state : 16;
	int type : 8;
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
} cache_connection;

typedef struct cache_connection_node {
	cache_connection connection;
	struct cache_connection_node* next;
} cache_connection_node;

#endif