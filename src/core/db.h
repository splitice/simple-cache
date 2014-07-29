#if !defined(DB_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7)
#define DB_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7

#include <stdbool.h>
#include <stdint.h>
#include "khash.h"
#include "db_structures.h"
#include "connection_structures.h"
#include "config.h"

/* Structure representing a free block (linked list node) */
struct block_free_node {
	uint32_t block_number;
	struct block_free_node* next;
};

/* Details regarding a database */
struct db_details {
	//Paths
	char path_root[MAX_PATH];
	char path_single[MAX_PATH];
	char path_blockfile[MAX_PATH];

	//Blockfile
	int fd_blockfile;

	//LRU
	struct cache_entry* lru_head;
	struct cache_entry* lru_tail;

	//block file
	struct block_free_node* free_blocks;
	uint32_t blocks_exist;

	//resource utilization
	uint64_t db_size_bytes;
	uint64_t db_keys;

	//Stats
	uint64_t db_stats_inserts;
	uint64_t db_stats_gets;
	uint64_t db_stats_deletes;
	uint64_t db_stats_operations;

	//Tables
	khash_t(table) *tables;
};

extern struct db_details db;

/* Open a database (from path) */
bool db_open(const char* path);

/* Close the dabase, cleaning up all memory allocations */
/* Only call after all other references (connections etc) are cleaned up */
void db_close();

/* Lookup Table in Database */
struct db_table* db_table_get_read(char* name, int length);
struct db_table* db_table_get_write(char* name, int length);

/* Lookup Cache Entry in Table */
struct cache_entry* db_entry_get_read(struct db_table* table, char* key, size_t length);
struct cache_entry* db_entry_get_write(struct db_table* table, char* key, size_t length);
struct cache_entry* db_entry_get_delete(struct db_table* table, char* key, size_t length);

/* Prepare for write */
void db_target_write_allocate(struct cache_target* target, uint32_t data_length);

/* Delete an entry (request) */
void db_entry_handle_delete(struct cache_entry* entry, khiter_t k);
void db_entry_handle_delete(struct cache_entry* entry);

/* Close entry, close fd, deref etc */
void db_table_close(struct db_table* table);
void db_target_entry_close(struct cache_target* target);
void db_target_setup(struct cache_target* target, struct cache_entry* entry, bool write);

void db_table_handle_delete(struct db_table* table);

#define IS_SINGLE_FILE(x) x->data_length>BLOCK_LENGTH

#endif // !defined(DB_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7)
