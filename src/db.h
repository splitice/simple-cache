#include <stdbool.h>
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