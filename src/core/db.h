#if !defined(DB_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7)
#define DB_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7

#include <stdbool.h>
#include <stdint.h>
#include "db_structures.h"
#include "connection_structures.h"
#include "config.h"

/* Structure representing a free block (linked list node) */
typedef struct block_free_node {
	uint32_t block_number;
	struct block_free_node* next;
} block_free_node;

/* Details regarding a database */
typedef struct db_details {
	//Paths
	char path_root[MAX_PATH];
	char path_single[MAX_PATH];
	char path_blockfile[MAX_PATH];

	//Blockfile
	int fd_blockfile;

	//Entries
	//TODO: table structure
	cache_entry** cache_hash_set;

	//LRU
	cache_entry* lru_head;
	cache_entry* lru_tail;

	//block file
	block_free_node* free_blocks;
	uint32_t blocks_allocated;

	//resource utilization
	uint64_t db_size_bytes;
	uint64_t db_keys;

	//Stats
	uint64_t db_stats_inserts;
	uint64_t db_stats_gets;
	uint64_t db_stats_deletes;
	uint64_t db_stats_operations;
} db_details;

extern struct db_details db;
bool db_open(const char* path);
int db_entry_open(cache_entry* e, int modes);
cache_entry* db_entry_get_read(char* key, size_t length);
cache_entry* db_entry_get_write(char* key, size_t length);
void db_entry_write_init(cache_target* target, uint32_t data_length);
void db_entry_delete(cache_entry* e);
void db_entry_close(cache_target* target);
void db_entry_handle_delete(cache_entry* entry);

#define IS_SINGLE_FILE(x) x->data_length>BLOCK_LENGTH

#endif // !defined(DB_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7)
