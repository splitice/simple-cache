#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "db.h"
#include "debug.h"
#include "hash.h"
#include "settings.h"

/* Globals */
struct db_details db {
	.path_root = { 0 },
	.path_single = { 0 },
	.path_blockfile = { 0 },
	.fd_blockfile = -1,
	.cache_hash_set = { 0 },
	.lru_head = NULL,
	.lru_tail = NULL,
	.free_blocks = NULL,
	.blocks_allocated = 0,
	.db_size_bytes = 0,
	.db_keys = 0
};

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

void db_lru_cleanup(int bytes_to_remove){
	while (bytes_to_remove > 0){
		assert(db.lru_head != NULL);

		cache_entry* l = db.lru_head;
		db.lru_head = db.lru_head->lru_next;

		bytes_to_remove -= l->data_length;

		db_entry_delete(l);
	}
}

void db_lru_gc(){
    if(settings.max_size < db.db_size_bytes){
        int bytes_to_remove = settings.max_size * settings.db_lru_clear;
        db_lru_cleanup(bytes_to_remove);
    }
}

void db_block_free(int block){
	block_free_node* old = db.free_blocks;
	db.free_blocks = (block_free_node*)malloc(sizeof(block_free_node));
	db.free_blocks->block_number = block;
	db.free_blocks->next = old;
}

int db_block_allocate_new(){
	int block_num = db.blocks_allocated;
	db.blocks_allocated++;
	if(ftruncate(db.fd_blockfile, db.blocks_allocated*BLOCK_LENGTH) < 0){
	    PWARN("File truncation failed");
	}
	return block_num;
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
	snprintf(out, MAX_PATH, "%s%c%c/%x", db.path_single, folder1, folder2, e->hash);
}

bool db_open(const char* path){
	//Create paths as char*'s
	snprintf(db.path_root, MAX_PATH, "%s/", path);
	snprintf(db.path_single, MAX_PATH, "%s/files/", path);

	//Initialize folder structure if it doesnt exist
	db_init_folders();

	//Block file
	snprintf(db.path_blockfile, MAX_PATH, "%s/block.db", path);
	db.fd_blockfile = open(db.path_blockfile, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
}


int db_entry_open(cache_entry* e, int modes){
	get_key_path(e, filename_buffer);
	int fd = open(filename_buffer, O_RDWR | modes, S_IRUSR | S_IWUSR);
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

	db.db_size_bytes -= e->data_length;
}

uint32_t hash_string(char* str, int length){
	uint32_t out;
	MurmurHash3_x86_32(str, length, 13, &out);
	return out;
}

cache_entry* db_entry_get_read(char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	int hash_key = hash % HASH_ENTRIES;
	cache_entry* entry = &db.cache_hash_set[hash_key];

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

	//LRU hit
	db_lru_hit(entry);

	return entry;
}

cache_entry* db_entry_get_write(char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	int hash_key = hash % HASH_ENTRIES;
	cache_entry* entry = &db.cache_hash_set[hash_key];

	//This is a re-used entry
	if (entry->key != NULL){
		//We have clients reading this key, cant write currently
		if (entry->refs){
			return NULL;
		}

		free(entry->key);
	}
	else{
		entry->block = -2;
	}

	entry->key = (char*)malloc(sizeof(char)* length);
	memcpy(entry->key, key, sizeof(char)* length);
	entry->key_length = length;
	entry->hash = hash;

	return entry;
}

void db_entry_write_init(cache_target* target, uint32_t data_length){
	cache_entry* entry = target->entry;
	if (entry->block == -2){
		//if this is a new entry, with nothing previously allocated.
		if (data_length > BLOCK_LENGTH){
			//Shorten or lengthen file to appropriate size
			if(ftruncate(target->fd, data_length)<0){
			    PWARN("File truncation failed");
			}
		}
		else{
			entry->block = db_block_get_write();
		}
	}
	else if (data_length > BLOCK_LENGTH){
		//If this is to be an entry stored in a file
		if (IS_SINGLE_FILE(entry)){
			//Shorten or lengthen file to appropriate size
			if(ftruncate(target->fd, data_length)<0){
			    PWARN("File truncation failed");
			}
		}
		else{
			//We are going to use a file, and the entry is currently a block
			db_block_free(entry->block);

			//No longer using a block
			entry->block = -1;

			//Lengthen file to required size
			if(ftruncate(target->fd, data_length)<0){
			    PWARN("File truncation failed");
			}
		}
	}
	else{
		//If this is to be an entry stored in a block
		if (IS_SINGLE_FILE(entry)){
			//We are going to store in a block, and the entry is currently a file
			get_key_path(entry, filename_buffer);

			//Delete single file, its not needed any more
			unlink(filename_buffer);
		}
		//Else: We are going to use a block, and the entry is currently a block
	}
	db.db_size_bytes += data_length - entry->data_length;
	entry->data_length = data_length;
}
