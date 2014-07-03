#include "db.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

void db_init_folders(){
	char folder_path[MAX_PATH];
	for (int i1 = 0; i1 < 26; i1++){
		for (int i2 = 0; i2 < 26; i2++){
			char folder1 = 'A' + i1;
			char folder2 = 'A' + i2;

			snprintf(folder_path, MAX_PATH, "%s/files/%c%c", db.path_single, folder1, folder2);

			mkdir(folder_path, 0777);
		}
	}
}

bool db_open(const char* path){
	snprintf(db.path_root, MAX_PATH, "%s/", path);
	snprintf(db.path_single, MAX_PATH, "%s/files/", path);
	db_init_folders();
	snprintf(db.path_blockfile, MAX_PATH, "%s/block.db", path);
}