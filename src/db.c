#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include "db.h"
#include "debug.h"
#include "hash.h"

/* Globals */
db.cache_hash_set = { 0 };
db.free_blocks = NULL;

//Buffers
char filename_buffer[MAX_PATH];

/* Methods */

void db_lru_hit(cache_entry* entry){
	if (entry->lru_prev != NULL){
		entry->lru_prev->lru_next = entry->lru_next;
	}
	else{
		//Is tail already, no need for further work
		return;
	}
	if (entry->lru_next != NULL){
		entry->lru_next->lru_prev = entry->lru_prev;
		entry->lru_next = NULL;
	}
	else{
		//Is head
		db.lru_head = NULL;
	}

	//Re-insert @ tail
	entry->lru_next = db.lru_tail;
	db.lru_tail = entry;
}

void db_lru_cleanup(int number_to_remove){
	while (number_to_remove != 0){
		assert(db.lru_head != NULL);

		cache_entry* l = db.lru_head;
		db.lru_head = db.lru_head->next;

		db_entry_delete(l);

		number_to_remove--;
	}
}

void db_block_free(int block){
	block_free_node* old = db.free_blocks;
	db.free_blocks = malloc(sizeof(block_free_node));
	db.free_blocks->block_number = block;
	db.free_blocks->next = old;
}

int db_block_allocate_new(){

}

int db_block_get_write(){
	if (db.free_blocks != NULL){
		int ret;
		block_free_node* block = db.free_blocks;
		db.free_blocks = db.free_blocks->next;
		ret = block->block_number;
		free(block);
		return ret;
	}
	else{
		return db_block_allocate_new();
	}
}

void db_init_folders(){
	mkdir(db.path_single, 0777);

	for (int i1 = 0; i1 < 26; i1++){
		for (int i2 = 0; i2 < 26; i2++){
			char folder1 = 'A' + i1;
			char folder2 = 'A' + i2;

			snprintf(filename_buffer, MAX_PATH, "%s%c%c", db.path_single, folder1, folder2);

			if (access(filename_buffer, F_OK) == -1){
				DEBUG("[#] Creating directory %s\n", filename_buffer);

				mkdir(filename_buffer, 0777);
			}
		}
	}
}

void get_key_path(cache_entry* e, char* out){
	char folder1 = 'A' + (e->hash % 26);
	char folder2 = 'A' + ((e->hash >> 8) % 26);
	snprintf(out, MAX_PATH, "%s%c%c/%s/%x", db.path_single, folder1, folder2, e->hash);
}

bool db_open(const char* path){
	//Create paths as char*'s
	snprintf(db.path_root, MAX_PATH, "%s/", path);
	snprintf(db.path_single, MAX_PATH, "%s/files/", path);

	//Initialize folder structure if it doesnt exist
	db_init_folders();

	//Block file
	snprintf(db.path_blockfile, MAX_PATH, "%s/block.db", path);
	db.fd_blockfile = open(db.path_blockfile, O_CREAT | O_RDWR);
}


int db_entry_open(cache_entry* e){
	get_key_path(e, filename_buffer);
	int fd = open(filename_buffer, O_RDWR);
	if (fd <= 0){
		WARN("Unable to open cache file: %s", filename_buffer);
	}
	return fd;
}

void db_entry_delete(cache_entry* e){
	if (IS_SINGLE_FILE(e)){
		get_key_path(e, filename_buffer);
		unlink(filename_buffer);
	}
	else{
		db_block_free(e->block);
	}

	//Clear key
	free(e->key);
	e->key = NULL;//Important: mark entry as empty

	//Debug only?
	e->key_length = 0;

	//Re-link next if needed
	if (e->lru_next != NULL){
		e->lru_next->lru_prev = e->lru_prev;
		if (e->lru_prev == NULL){
			db.lru_tail = e->lru_next;
		}
	}

	//Re-link previous if needed
	if (e->lru_prev != NULL){
		e->lru_prev->lru_next = e->lru_next;
		if (e->lru_prev == NULL){
			db.lru_head = e->lru_prev;
		}
	}
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
		DEBUG("[#] Unable to look up key: ");

		if (entry->key == NULL){
			DEBUG("DB Key is null\n");
		}
		else{
			if (entry->key_length != length){
				DEBUG("DB Key length does not match\n");
			}
			else{
				if (strncmp(key, entry->key, length)){
					DEBUG("String Keys dont match\n");
				}
			}
		}


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
	memcpy(entry->key, key, sizeof(char)* length);
	entry->key_length = length;
	entry->hash = hash;

	return entry;
}

void db_entry_write_init(cache_entry* entry, uint32_t data_length){
	if (data_length > BLOCK_LENGTH){
		if (IS_SINGLE_FILE(entry)){
			//We are going to store in a file, and the entry is currently a file
			get_key_path(entry, filename_buffer);

			//Shorten or lengthen file to appropriate size
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

			//Delete single file, its not needed any more
			unlink(filename_buffer);
		}
		//Else: We are going to use a block, and the entry is currently a block
	}
	entry->data_length = data_length;
}
