#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint16_t refs;
	uint32_t hash;
	char* key;
	uint16_t key_length;
	uint32_t data_length;
	uint16_t block;
	bool writing : 1;
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