#if !defined(SCACHE_H_INCLUDED_9AA4BC83_3F1B_42F0_9291_23880637CC16)
#define SCACHE_H_INCLUDED_9AA4BC83_3F1B_42F0_9291_23880637CC16

#include <stdbool.h>
#include <stdint.h>

typedef struct cache_entry {
	//key
	uint32_t hash;
	char* key;
	uint16_t key_length;

	//data
	uint32_t data_length;
	uint16_t block;

	//status
	uint16_t refs;
	bool writing : 1;

	//lru
	struct cache_entry* lru_next;
	struct cache_entry* lru_prev;
} cache_entry;

typedef struct {
	cache_entry* entry;
	size_t position;
	size_t end_position;
	int fd;
} cache_target;

typedef struct {
	cache_target target;
	int state : 16;
	int type : 16;
	int client_sock;

	//Reading from socket buffers
	int input_read_position;
	uint32_t input_expect;
	int input_buffer_write_position;
	char input_buffer[4096];
	
	//Writing to socket buffers
	char* output_buffer;
	int output_length;
	char* output_buffer_free;
} cache_connection;

typedef struct cache_connection_node {
	cache_connection connection;
	struct cache_connection_node* next;
} cache_connection_node;

typedef struct block_free_node {
	int block_number;
	struct block_free_node* next;
} block_free_node;

#endif // !defined(SCACHE_H_INCLUDED_9AA4BC83_3F1B_42F0_9291_23880637CC16)