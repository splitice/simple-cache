#if !defined(DB_STRUCTURES_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7)
#define DB_STRUCTURES_H_INCLUDED_1FA53FD2_BB5F_432E_9E57_761594DEBEC7

#include "stdint.h"
#include "stdbool.h"

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

#endif