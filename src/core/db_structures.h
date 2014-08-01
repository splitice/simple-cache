#if !defined(DB_STRUCTURES_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7)
#define DB_STRUCTURES_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7

#include "stdint.h"
#include "stdbool.h"
#include "khash.h"


struct db_table;

struct cache_entry
{
	uint32_t hash;
	char* key;
	struct cache_entry* lru_next;
	struct cache_entry* lru_prev;
	uint32_t data_length;
	uint32_t block;
	__time_t expires;
	uint16_t key_length;
	uint16_t refs;
	db_table* table;
	bool writing : 1;
	bool deleted : 1;
#ifdef DEBUG_BUILD
	bool lru_found : 1;
	bool lru_removed : 1;
#endif
};

KHASH_MAP_INIT_INT(entry, struct cache_entry*)

struct db_table {
	uint32_t hash;
	char* key;

	khash_t(entry) *cache_hash_set;
	uint16_t refs;
	bool deleted : 1;
};


KHASH_MAP_INIT_INT(table, struct db_table*)

#endif