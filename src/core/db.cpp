/*
Functions and structures for database storage

Block File
===========
------------------------------------------------------
|        Block 1        |       Block 2        |
|  (BLOCK_SIZE bytes)   |  (BLOCK_SIZE bytes)  |  ...
------------------------------------------------------

LRU
===

 Least Recently Used <------------------------> Most Recently Used

-------------------------------------------------------------------
| Entry #0    | Entry #2     |      | Entry #10    | Entry #11    |
| next: NULL  | next: #0     |  ... | next: #9     | next: NULL   |
| prev: #1    | prev: #2     |      | prev: #11    | prev: #10    |
-------------------------------------------------------------------

        Head <-----------------------------------------> Tail
*/
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
	.cache_hash_set = NULL,
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

void db_lru_remove_node(cache_entry* entry){
	if (entry->lru_prev != NULL){
		entry->lru_prev->lru_next = entry->lru_next;
	}
	else{
		//This node is the tail
		db.lru_tail = entry->lru_next;
	}

	if (entry->lru_next != NULL){
		entry->lru_next->lru_prev = entry->lru_prev;
	}
	else{
		//Is head
		db.lru_head = entry->lru_prev;
	}
}

void db_lru_delete_node(cache_entry* entry){
	db_lru_remove_node(entry);
	if (entry == db.lru_head){
		db.lru_head = entry->lru_prev;
	}
	if (entry == db.lru_tail){
		db.lru_tail = entry->lru_next;
	}
}

void db_lru_hit(cache_entry* entry){
	//Remove from current position
	db_lru_remove_node(entry);

	//Re-insert @ tail
	entry->lru_prev = NULL;	
	entry->lru_next = db.lru_tail;
	db.lru_tail = entry;
}

void db_lru_insert(cache_entry* entry){
	//insert @ tail
	entry->lru_prev = NULL;
	entry->lru_next = db.lru_tail;
	db.lru_tail = entry;
}

void db_lru_cleanup(int bytes_to_remove){
	while (bytes_to_remove > 0 && db.lru_head != NULL){
		cache_entry* l = db.lru_head;
		db.lru_head = db.lru_head->lru_next;
		db.lru_head->lru_prev = NULL;

		bytes_to_remove -= l->data_length;

		db_entry_delete(l);
	}

	//null the tail as well
	if (db.lru_head == NULL){
		db.lru_tail = NULL;
	}
}

void db_lru_gc(){
	if (settings.max_size > 0 && settings.max_size < db.db_size_bytes){
		int bytes_to_remove = (db.db_size_bytes - settings.max_size) + (settings.max_size * settings.db_lru_clear);
		
        db_lru_cleanup(bytes_to_remove);
    }
}

void db_block_free(uint32_t block){
	block_free_node* old = db.free_blocks;
	db.free_blocks = (block_free_node*)malloc(sizeof(block_free_node));
	db.free_blocks->block_number = block;
	db.free_blocks->next = old;
}

int db_block_allocate_new(){
	uint32_t block_num = db.blocks_allocated;
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
		return -2;
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
	if (db.fd_blockfile < 0){
		PFATAL("Failed to open blockfile: %s", db.path_blockfile);
	}

	//cache entries
	db.cache_hash_set = (cache_entry**)calloc(HASH_ENTRIES, sizeof(cache_entry*));
}


int db_entry_open(cache_entry* e, int modes){
	get_key_path(e, filename_buffer);
	int fd = open(filename_buffer, O_RDWR | modes, S_IRUSR | S_IWUSR);
	if (fd <= 0){
		WARN("Unable to open cache file: %s", filename_buffer);
	}
	return fd;
}

void db_entry_deref(cache_entry* entry){
	entry->refs--;

	//Actually clean up the entry
	if (entry->refs == 0 && entry->deleted){
		//If is a block, can now free it
		if (!IS_SINGLE_FILE(entry)){
			db_block_free(entry->block);
		}

		//Clear entry - file has already been deleted.
		//At this stage entry has already been removed from LRU and hash table
		//exists only to complete files being served.
		int hash_key = entry->hash % HASH_ENTRIES;
		db.cache_hash_set[hash_key] = NULL;
		free(entry);
	}
}

void db_entry_incref(cache_entry* entry){
	entry->refs++;
}

void db_entry_close(cache_target* target){
	cache_entry* entry = target->entry;
	if (target->fd != db.fd_blockfile){
		close(target->fd);
	}
	db_entry_deref(target->entry);
	target->position = 0;
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

	//Update LRU
	db_lru_delete_node(e);

	//Stats
	db.db_stats_deletes++;
	db.db_stats_operations++;
}

uint32_t hash_string(const char* str, int length){
	uint32_t out;
	MurmurHash3_x86_32(str, length, 13, &out);
	return out;
}

cache_entry* db_entry_get_read(char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	int hash_key = hash % HASH_ENTRIES;
	cache_entry* entry = db.cache_hash_set[hash_key];

	if (entry == NULL || entry->key_length != length || strncmp(key, entry->key, length)){
		DEBUG("[#] Unable to look up key: ");

		if (entry == NULL){
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

		free(key);
		return NULL;
	}

	//Free key text, not needed.
	free(key);

	//LRU hit
	db_lru_hit(entry);

	//Stats
	db.db_stats_gets++;
	db.db_stats_operations++;

	//Refs
	db_entry_incref(entry);

	return entry;
}

cache_entry* db_entry_new(){
	cache_entry* entry = (cache_entry*)malloc(sizeof(cache_entry));
	entry->refs = 0;
	entry->writing = false;
	entry->deleted = false;
	return entry;
}

cache_entry* db_entry_get_write(char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	int hash_key = hash % HASH_ENTRIES;
	cache_entry* entry = db.cache_hash_set[hash_key];

	//Stats
	db.db_stats_inserts++;
	db.db_stats_operations++;

	//This is a re-used entry
	if (entry != NULL){
		//If we are currently writing, then it will be mocked
		if (entry->writing == true){
			return NULL;
		}

		//We have clients reading this key, cant write currently
		db_entry_handle_delete(entry);

		entry = db_entry_new();
	}
	else{
		entry = db_entry_new();
		entry->block = db_block_allocate_new();
	}

	//Store entry
	db.cache_hash_set[hash_key] = entry;

	//LRU: insert
	db_lru_insert(entry);

	entry->key = key;
	entry->key_length = length;
	entry->hash = hash;

	//Refs
	db_entry_incref(entry);

	//LRU
	if ((db.db_stats_inserts % DB_LRU_EVERY) == 0){
		DEBUG("[#] Do LRU GC check.\n");
		db_lru_gc();
	}

	return entry;
}

cache_entry* db_entry_get_delete(char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	int hash_key = hash % HASH_ENTRIES;
	cache_entry* entry = db.cache_hash_set[hash_key];

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

		free(key);
		return NULL;
	}

	//Free key text, not needed.
	free(key);

	//Stats
	db.db_stats_deletes++;
	db.db_stats_operations++;

	//Refs
	db_entry_incref(entry);

	return entry;
}

void db_entry_handle_delete(cache_entry* entry){
	assert(!entry->deleted);

	if (IS_SINGLE_FILE(entry)){
		//Unlink
		get_key_path(entry, filename_buffer);
		unlink(filename_buffer);
	}

	//Counters
	db.db_size_bytes -= entry->data_length;

	//Remove from LRU
	db_lru_delete_node(entry);

	//Remove from hash table
	int hash_key = entry->hash % HASH_ENTRIES;
	db.cache_hash_set[hash_key] = NULL;

	//Dont need the key any more, deleted
	free(entry->key);
	entry->deleted = true;
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

	//Update database counters
	db.db_size_bytes += data_length;
	entry->data_length = data_length;
}
