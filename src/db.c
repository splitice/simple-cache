#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "db.h"

/* Globals */
//Buffers
char filename_buffer[MAX_PATH];

//Cache Memory
cache_entry cache_hash_set[HASH_ENTRIES] = { 0 };
block_free_node* free_blocks = NULL;

/* Methods */

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

void get_key_path(cache_entry* e, char* out){
	char folder1 = 'A' + (e->hash % 26);
	char folder2 = 'A' + ((e->hash >> 8) % 26);
	snprintf(out, MAX_PATH, "%s%c%c/%s/%x", db.path_single, folder1, folder2, e->hash);
}

bool db_open(const char* path){
	snprintf(db.path_root, MAX_PATH, "%s/", path);
	snprintf(db.path_single, MAX_PATH, "%s/files/", path);
	db_init_folders();
	snprintf(db.path_blockfile, MAX_PATH, "%s/block.db", path);
}


int db_entry_open(cache_entry* e){
	get_key_path(e, filename_buffer);
	int fd = open(filename_buffer, O_RDWR);
	if (fd <= 0){
		WARN("Unable to open cache file: %s", filename_buffer);
	}
	return fd;
}

uint32_t hash_string(char* str, int length){
	uint32_t out;
	MurmurHash3_x86_32(str, length, 13, &out);
	return out;
}

cache_entry* db_entry_get_read(char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	int hash_key = hash % HASH_ENTRIES;
	cache_entry* entry = &cache_hash_set[hash_key];
	if (entry->key == NULL || entry->key_length != length || strncmp(key, entry->key, length)){
		return NULL;
	}

	return entry;
}

cache_entry* db_entry_get_write(char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	int hash_key = hash % HASH_ENTRIES;
	cache_entry* entry = &cache_hash_set[hash_key];

	//This is a re-used entry
	if (entry->key != NULL){
		//We have clients reading this key, cant write currently
		if (entry->refs){
			return NULL;
		}

		free(entry->key);
	}

	entry->key = (char*)malloc(sizeof(char)* length);
	memcpy(key, entry->key, sizeof(char)* length);
	entry->key_length = length;
	entry->hash = hash;

	return entry;
}

void db_entry_write_init(cache_entry* entry, uint32_t data_length){
	if (data_length > BLOCK_LENGTH){
		if (IS_SINGLE_FILE(entry)){
			//We are going to store in a file, and the entry is currently a file
			get_key_path(entry, filename_buffer);
			truncate(filename_buffer, data_length);
		}
		else{
			//We are going to use a file, and the entry is currently a block

		}
	}
	else{
		if (IS_SINGLE_FILE(entry)){
			//We are going to store in a block, and the entry is currently a file
			get_key_path(entry, filename_buffer);
			unlink(filename_buffer);
		}
		//Else: We are going to use a block, and the entry is currently a block
	}
}
