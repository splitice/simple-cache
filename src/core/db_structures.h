#if !defined(DB_STRUCTURES_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7)
#define DB_STRUCTURES_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7

#include "stdint.h"
#include "stdbool.h"
#include "khash.h"


struct db_table;

struct cache_entry
{
	db_table* table;
	uint32_t hash;
	char* key;
	uint16_t key_length : 14;
	bool writing : 1;
	bool deleted : 1;
	uint16_t refs;
	uint32_t data_length;
	uint32_t block;
	__time_t expires;
	struct cache_entry* lru_next;
	struct cache_entry* lru_prev;
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
	uint16_t refs: 15;
	bool deleted : 1;
};


KHASH_MAP_INIT_INT(table, struct db_table*)

#endif