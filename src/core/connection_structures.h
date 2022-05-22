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

static const char *state_action_string[] = {
    "close_connection", "registered_write", "needs_more_read", "continue_processing"
};

typedef enum {
	cache_listener, mon_listener, cache_connection, mon_connection
} connection_ctype;

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
		 epollout,
		 epollrdhup;

	connection_ctype ltype;

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

struct listener_entry
{
	int fd;
	connection_ctype type;
};


struct listener_collection
{
	struct listener_entry* listeners;
	uint32_t listener_count;
};

static inline const char* connection_type_string(connection_ctype l){
	switch(l){
		case cache_listener:
			return "cache listener";
		case mon_listener:
			return "monitoring listener";
		case cache_connection:
			return "cache connection";
		case mon_connection:
			return "monitoring connection";
		default:
			return "unknown";
	}
}

#endif