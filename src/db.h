#include <stdbool.h>
#include "scache.h"
#include "config.h"

typedef struct db_details {
	char path_root[MAX_PATH];
	char path_single[MAX_PATH];
	char path_blockfile[MAX_PATH];

	//Blockfile
	int fd_blockfile;
} db_details;

struct db_details db;
bool db_open(const char* path);
int db_entry_open(cache_entry* e);
cache_entry* db_entry_get_read(char* key, size_t length);
cache_entry* db_entry_get_write(char* key, size_t length);
void db_entry_write_init(cache_entry* entry, uint32_t data_length);

#define IS_SINGLE_FILE(x) x->data_length>BLOCK_LENGTH