#if !defined(DB_STRUCTURES_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7)
#define DB_STRUCTURES_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7

#include "stdint.h"
#include "stdbool.h"

typedef struct cache_entry {
	uint32_t hash;
	char* key;
	struct cache_entry* lru_next;
	struct cache_entry* lru_prev;
	uint32_t data_length;
	uint32_t block;
	uint16_t key_length;
	uint16_t refs;
	bool writing : 1;
	bool deleted : 1;
} cache_entry;

#endif