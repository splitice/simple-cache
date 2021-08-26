#if !defined(CONNECTION_STRUCTURES_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
#define CONNECTION_STRUCTURES_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D

#include "db_structures.h"
#include "read_buffer.h"

struct cache_target {
	struct cache_entry* entry;
	off64_t position;
	off64_t end_position;
	int fd; // fd can be init to -1 because its not overlapping with table_target
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
	close_connection, registered_write, needs_more_read, continue_processing
} state_action;

typedef enum {
	cache_listener, mon_listener
} listener_type;

struct scache_connection {
	//Writing to socket buffers
	const char* output_buffer;
	int output_length;
	char* output_buffer_free;
	state_action(*handler)(scache_connection* connection);
	const char* handler_name;
	uint32_t state;
	int client_sock;
	struct read_buffer input;
	
	uint16_t method;
	bool cache_writing,
	     epollin,
		 epollout;

	listener_type ltype;

	union {	
		struct {
			utarget target;
		} cache;
		struct {
			scache_connection* prev;
			scache_connection* next;
			timeval scheduled;
			uint16_t current;
		} monitoring;
	};
};

struct scache_connection_node {
	struct scache_connection connection;
	struct scache_connection_node* next;
};

struct listener_entry
{
	int fd;
	listener_type type;
};


struct listener_collection
{
	struct listener_entry* listeners;
	uint32_t listener_count;
};

static inline const char* listener_type_string(listener_type l){
	switch(l){
		case cache_listener:
			return "cache";
		case mon_listener:
			return "monitoring";
		default:
			return "unknown";
	}
}

#endif