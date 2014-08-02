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
| Entry #0    | Entry #1     |      | Entry #10    | Entry #11    |
| next: #1    | next: #2     |  ... | next: #11    | next: NULL   |
| prev: NULL  | prev: #0     |      | prev: #9     | prev: #10    |
-------------------------------------------------------------------

        Head <-----------------------------------------> Tail
*/
#include <stdio.h>
#include <dirent.h>
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
#include "timer.h"

/* Globals */
struct db_details db {
	.path_root = { 0 },
	.path_single = { 0 },
	.path_blockfile = { 0 },
	.fd_blockfile = -1,
	.lru_head = NULL,
	.lru_tail = NULL,
	.free_blocks = NULL,
	.blocks_exist = 0,
	.db_size_bytes = 0,
	.db_keys = 0,
	.db_stats_inserts = 0,
	.db_stats_gets = 0,
	.db_stats_deletes = 0,
	.db_stats_operations = 0,
	.tables = NULL
};

//Buffers
char filename_buffer[MAX_PATH];

#ifdef DEBUG_BUILD
void db_validate_lru_flags(){
	for (khiter_t k = kh_begin(db.tables); k != kh_end(db.tables); ++k){
		if (kh_exist(db.tables, k)) {
			db_table* table = kh_val(db.tables, k);
			for (khiter_t ke = kh_begin(table->cache_hash_set); ke != kh_end(table->cache_hash_set); ++ke){
				if (kh_exist(table->cache_hash_set, ke)) {
					cache_entry* entry = kh_val(table->cache_hash_set, ke);
					assert(entry->lru_found);
					entry->lru_found = false;
				}
			}
		}
	}
}
#endif

void db_validate_lru(){
#ifdef DEBUG_BUILD
	cache_entry* entry = db.lru_head;
	while (entry != NULL){
		assert(!entry->lru_found);
		entry->lru_found = true;
		entry = entry->lru_next;
	}
	db_validate_lru_flags();

	entry = db.lru_tail;
	while (entry != NULL){
		assert(!entry->lru_found);
		entry->lru_found = true;
		entry = entry->lru_prev;
	}
	db_validate_lru_flags();
#endif
}

void db_lru_remove_node(cache_entry* entry){
	assert(!entry->lru_removed);
	if (entry->lru_prev != NULL){
		assert(db.lru_head != entry);
		entry->lru_prev->lru_next = entry->lru_next;
	}
	else{
		//This node is the tail
		assert(db.lru_head == entry);
		db.lru_head = entry->lru_next;
	}

	if (entry->lru_next != NULL){
		assert(db.lru_tail != entry);
		entry->lru_next->lru_prev = entry->lru_prev;
	}
	else{
		assert(db.lru_tail == entry);
		db.lru_tail = entry->lru_prev;
	}
	entry->lru_next = NULL;
	entry->lru_prev = NULL;
#ifdef DEBUG_BUILD
	entry->lru_removed = true;
#endif
}


void db_lru_insert(cache_entry* entry){
	assert(entry->lru_removed);
	assert(entry != db.lru_tail);
	assert(entry->lru_next == NULL);
	assert(entry->lru_prev == NULL);

	//insert @ tail
	entry->lru_prev = db.lru_tail;
	if (db.lru_tail != NULL){
		assert(db.lru_tail->lru_next == NULL);
		db.lru_tail->lru_next = entry;
	}
	entry->lru_next = NULL;
	db.lru_tail = entry;
	if (db.lru_head == NULL){
		db.lru_head = entry;
	}

#ifdef DEBUG_BUILD
	entry->lru_removed = false;
#endif
}

void db_lru_hit(cache_entry* entry){
	assert(!entry->lru_removed);

	//Remove from current position
	db_lru_remove_node(entry);

	//Re-insert @ tail
	db_lru_insert(entry);

	db_validate_lru();
}

void db_block_free(uint32_t block){
	block_free_node* old = db.free_blocks;
	db.free_blocks = (block_free_node*)malloc(sizeof(block_free_node));
	db.free_blocks->block_number = block;
	db.free_blocks->next = old;
}

void db_entry_actually_delete(cache_entry* entry){
	DEBUG("[#] Cleaning key up reference due to refcount == 0\n");
#ifdef DEBUG_BUILD
	assert(entry->lru_removed);
#endif
	//If is a block, can now free it
	if (!IS_SINGLE_FILE(entry)){
		db_block_free(entry->block);
	}


	//Free key
	free(entry->key);
	free(entry);
}

void db_table_actually_delete(db_table* entry){
	DEBUG("[#] Cleaning table up reference due to refcount == 0\n");

	//Remove table from database
	khiter_t k = kh_get(table, db.tables, entry->hash);
	if (k != kh_end(db.tables)){
		kh_del(table, db.tables, k);
	}

	//Free key
	free(entry->key);
	free(entry);
}

void db_table_deref(db_table* entry){
	DEBUG("[#] Decrementing table refcount - was: %d\n", entry->refs);
	assert(entry->refs > 0);
	entry->refs--;

	//Actually clean up the entry
	if (entry->refs == 0 && entry->deleted){
		//Remove table from hash set
		db_table_actually_delete(entry);
	}
}

void db_table_incref(db_table* entry){
	DEBUG("[#] Incrementing refcount - was: %d\n", entry->refs);
	entry->refs++;
}

void db_entry_deref(cache_entry* entry){
	DEBUG("[#] Decrementing refcount - was: %d\n", entry->refs);
	entry->refs--;

	//Actually clean up the entry
	if (entry->refs == 0 && entry->deleted){
		db_entry_actually_delete(entry);
	}
	db_table_deref(entry->table);
}

void db_entry_incref(cache_entry* entry, bool table = true){
	DEBUG("[#] Incrementing entry refcount - was: %d\n", entry->refs);
	entry->refs++;
	if (table)
		db_table_incref(entry->table);
}

void db_lru_cleanup(int bytes_to_remove){
	while (bytes_to_remove > 0 && db.lru_head != NULL){
		cache_entry* l = db.lru_head;

		//Skip if currently deleting
		assert(!l->deleted);

		bytes_to_remove -= l->data_length;

		if (l->refs == 0){
			db_entry_incref(l);
			db_entry_handle_delete(l);
			db_entry_deref(l);
		}
		else{
			db_entry_handle_delete(l);
		}
	}
}

void db_lru_gc(){
	if (settings.max_size > 0 && settings.max_size < db.db_size_bytes){
		double bytes_to_remove = (((double)db.db_size_bytes / settings.max_size) - (1. - settings.db_lru_clear)) * db.db_keys;
		
        db_lru_cleanup((int)bytes_to_remove);
    }
}

int db_block_allocate_new(){
	uint32_t block_num = db.blocks_exist;
	db.blocks_exist++;
	if (ftruncate(db.fd_blockfile, db.blocks_exist*BLOCK_LENGTH) < 0){
		PWARN("File truncation failed (length: %d)", db.blocks_exist);
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
	char file_buffer[MAX_PATH];

	mkdir(db.path_single, 0777);

	for (int i1 = 0; i1 < 26; i1++){
		for (int i2 = 0; i2 < 26; i2++){
			char folder1 = 'A' + i1;
			char folder2 = 'A' + i2;

			snprintf(filename_buffer, MAX_PATH, "%s%c%c", db.path_single, folder1, folder2);

			if (access(filename_buffer, F_OK) == -1){
				//DEBUG("[#] Creating directory %s\n", filename_buffer);

				mkdir(filename_buffer, 0777);
			}
			else{
				struct dirent *next_file;
				DIR *theFolder = opendir(filename_buffer);
				while (next_file = readdir(theFolder))
				{
					if (*(next_file->d_name) == '.')
						continue;
					// build the full path for each file in the folder
					sprintf(file_buffer, "%s/%s", filename_buffer, next_file->d_name);
					remove(file_buffer);
				}
				if (closedir(theFolder) < 0){
					PFATAL("Unable to close directory.");
				}
			}
		}
	}
}

void db_complete_writing(cache_entry* entry){
	assert(entry->writing);
	entry->writing = false;

	if (!entry->deleted){
		//LRU: insert
		db_lru_insert(entry);
	}
}

uint32_t hash_string(const char* str, int length){
	uint32_t out;
	MurmurHash3_x86_32(str, length, 13, &out);
	return out;
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
	snprintf(db.path_blockfile, MAX_PATH, "%s/blockfile.db", path);
	db.fd_blockfile = open(db.path_blockfile, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (db.fd_blockfile < 0){
		PFATAL("Failed to open blockfile: %s", db.path_blockfile);
	}

	//Mark all blocks that already exist in the block file as non-allocated
	int size = lseek(db.fd_blockfile, 0L, SEEK_END);
	for (uint32_t i = 0; i < size; i += BLOCK_LENGTH){
		db_block_free(i / BLOCK_LENGTH);
		db.blocks_exist++;
	}

	//cache entries
	//db.cache_hash_set = (cache_entry**)calloc(HASH_ENTRIES, sizeof(cache_entry*));
	db.tables = kh_init(table);
}

int db_entry_open(struct cache_entry* e, mode_t modes){
	get_key_path(e, filename_buffer);
	int fd = open(filename_buffer, O_RDWR | modes, S_IRUSR | S_IWUSR);
	if (fd <= 0){
		WARN("Unable to open cache file: %s", filename_buffer);
	}
	return fd;
}

void db_target_open(struct cache_target* target){
	if (IS_SINGLE_FILE(target->entry)){
		target->position = 0;
		target->fd = db_entry_open(target->entry, write ? O_CREAT : 0);
	}
	else{
		target->fd = db.fd_blockfile;
		target->position = target->entry->block * BLOCK_LENGTH;
	}
	target->end_position = target->position + target->entry->data_length;
}

void db_target_setup(struct cache_target* target, struct cache_entry* entry, bool write){
	if (!write){
		db_target_open(target);
	}
	else{
		target->fd = -1;
	}
}

void db_target_entry_close(cache_target* target){
	if (target->entry != NULL){
		if (target->fd != db.fd_blockfile && target->fd != -1){
			assert(target->fd > 0 || settings.daemon_mode);
			close(target->fd);
		}

		db_entry_deref(target->entry);
		target->entry = NULL;
		target->fd = -1;
	}
	target->position = 0;
}

void db_table_close(db_table* table){
	db_table_deref(table);
}

cache_entry* db_entry_get_read(struct db_table* table, char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	khiter_t k = kh_get(entry, table->cache_hash_set, hash);
	cache_entry* entry = k == kh_end(table->cache_hash_set) ? NULL : kh_value(table->cache_hash_set, k);

	if (entry == NULL){
		DEBUG("[#] Key does not exist\n");
		free(key);
		return NULL;
	}

	if (entry->expires != 0){
		DEBUG("[#] Key has ttl: %lu (%d from now)\n", (unsigned long)entry->expires, (int)(entry->expires - time_seconds));
	}

	if (entry->expires != 0 && entry->expires < time_seconds){
		DEBUG("[#] Key expired\n");
		free(key);
		db_entry_incref(entry);
		db_entry_handle_delete(entry);
		db_entry_deref(entry);
		return NULL;
	}

	assert(!entry->deleted);

	if (entry->key_length != length || strncmp(key, entry->key, length)){
		DEBUG("[#] Unable to look up key: ");

		if (entry->key_length != length){
			DEBUG("DB Key length does not match\n");
		}
		else{
			if (strncmp(key, entry->key, length)){
				DEBUG("String Keys dont match\n");
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

	//Check if currently writing (unfinished)
	if (entry->writing){
		//TODO: possibly future, subscribe and writer handles data delivery
		return NULL;
	}

	//Refs
	db_entry_incref(entry, false);

	return entry;
}

cache_entry* db_entry_new(db_table* table){
	cache_entry* entry = (cache_entry*)malloc(sizeof(cache_entry));
	entry->refs = 0;
	entry->writing = false;
	entry->deleted = false;
	entry->expires = 0;
	entry->table = table;
	entry->lru_next = NULL;
	entry->lru_prev = NULL;

#ifdef DEBUG_BUILD
	entry->lru_found = false;
	entry->lru_removed = true;
#endif
	return entry;
}

struct db_table* db_table_get_read(char* name, int length){
	uint32_t hash = hash_string(name, length);

	khiter_t k = kh_get(table, db.tables, hash);

	if (k == kh_end(db.tables)){
		free(name);
		return NULL;
	}

	db_table* entry = kh_value(db.tables, k);

	free(name);
	assert(entry != NULL);

	db_table_incref(entry);

	return entry;
}
struct db_table* db_table_get_write(char* name, int length){
	uint32_t hash = hash_string(name, length);

	khiter_t k = kh_get(table, db.tables, hash);
	db_table* table;

	if (k == kh_end(db.tables)){
		table = (db_table*)malloc(sizeof(db_table));
		table->hash = hash;
		table->key = name;
		table->refs = 1;
		table->deleted = false;
		table->cache_hash_set = kh_init(entry);

		int ret;
		k = kh_put(table, db.tables, hash, &ret);
		kh_value(db.tables, k) = table;
		//db_table_incref(table);
		return table;
	}

	table = kh_value(db.tables, k);

	free(name);
	assert(table != NULL);

	db_table_incref(table);

	return table;
}

void db_entry_handle_softdelete(cache_entry* entry, khiter_t k){
	assert(!entry->deleted);

	if (IS_SINGLE_FILE(entry)){
		//Unlink
		get_key_path(entry, filename_buffer);
		unlink(filename_buffer);
	}

	//Dont need the key any more, deleted
	entry->deleted = true;
	kh_del(entry, entry->table->cache_hash_set, k);

	//Counters
	db.db_size_bytes -= entry->data_length;

	//Remove from LRU
	db_lru_remove_node(entry);

	//Assertion check
	if (entry->refs == 0){
		DEBUG("[#] Entry can be immediately cleaned up\n");
		db_entry_actually_delete(entry);
	}
}


cache_entry* db_entry_get_write(struct db_table* table, char* key, size_t length){
	assert(table->refs >= 1); //If not, it wouldnt be existing

	uint32_t hash = hash_string(key, length);
	khiter_t k = kh_get(entry, table->cache_hash_set, hash);
	cache_entry* entry = k == kh_end(table->cache_hash_set) ? NULL : kh_value(table->cache_hash_set, k);

	//Stats
	db.db_stats_inserts++;
	db.db_stats_operations++;


	//This is a re-used entry
	if (entry != NULL){
		//If we are currently writing, then it will be mocked
		if (entry->writing == true){
			return NULL;
		}

		//We might have clients reading this key
		db_entry_handle_softdelete(entry, k);

		db_validate_lru();
	}
	else{
		db.db_keys++;
	}

	entry = db_entry_new(table);
	entry->block = db_block_get_write();
	entry->data_length = 0;
	entry->key = key;
	entry->key_length = length;
	entry->hash = hash;

	//Take a reference if this is the first entry (released when size == 0)
	if (kh_size(table->cache_hash_set) == 0){
		db_table_incref(table);
	}
	assert(table->refs >= 2 || (table->refs >= 1 && table->deleted)); //If not, it wouldnt be storing

	//Store entry
	int ret;
	k = kh_put(entry, entry->table->cache_hash_set, entry->hash, &ret);
	kh_value(entry->table->cache_hash_set, k) = entry;

	//Refs
	db_entry_incref(entry, false);
	entry->writing = true;

	assert(!entry->deleted);

	//LRU
	if ((db.db_stats_inserts % DB_LRU_EVERY) == 0){
		DEBUG("[#] Do LRU GC check.\n");
		db_lru_gc();
	}

	return entry;
}

cache_entry* db_entry_get_delete(struct db_table* table, char* key, size_t length){
	uint32_t hash = hash_string(key, length);
	khiter_t k = kh_get(entry, table->cache_hash_set, hash);
	cache_entry* entry = k == kh_end(table->cache_hash_set) ? NULL : kh_value(table->cache_hash_set, k);

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

	assert(!entry->deleted);

	//Stats
	db.db_stats_deletes++;
	db.db_stats_operations++;

	//Refs
	db_entry_incref(entry, false);

	return entry;
}

void db_close(){
	//tables and key space
	for (khiter_t ke = kh_begin(db.tables); ke != kh_end(db.tables); ++ke){
		if (kh_exist(db.tables, ke)) {
			db_table* table = kh_val(db.tables, ke);

			//All other refernces should have been de-refed before db_close is called
			//and hence anything pending deletion will have been cleaned up already
			assert(!table->deleted);

			//Check reference count (should be 1)
			assert(table->refs == 1);

			//Actually delete
			db_table_handle_delete(table);
		}
	}
	kh_destroy(table, db.tables);

	//blocks
	block_free_node* bf = db.free_blocks;
	while (bf != NULL){
		block_free_node* bf2 = bf;
		bf = bf->next;
		free(bf2);
	}
	db.free_blocks = NULL;
}

void db_entry_handle_delete(cache_entry* entry){
	khiter_t k = kh_get(entry, entry->table->cache_hash_set, entry->hash);

	db_entry_handle_delete(entry, k);
}

void db_delete_table_entry(db_table* table, khiter_t k){
	kh_destroy(entry, table->cache_hash_set);

	//If not fully de-refed remove now, not later
	if (table->refs != 0){
		assert(k != kh_end(db.tables));
		kh_del(table, db.tables, k);
	}

	//Remove reference holding table open
	db_table_deref(table);
}


void db_table_handle_delete(db_table* table, khiter_t k){
	//Set deleted
	assert(!table->deleted);
	table->deleted = true;

	//Delete keys from table
	for (khiter_t ke = kh_begin(table->cache_hash_set); ke != kh_end(table->cache_hash_set); ++ke){
		if (kh_exist(table->cache_hash_set, ke)) {
			cache_entry* ce = kh_val(table->cache_hash_set, ke);
			if (!ce->deleted){
				db_entry_handle_softdelete(ce, ke);
			}
		}
	}
	
	db_delete_table_entry(table, k);
}


void db_table_handle_delete(db_table* table){
	khiter_t k = kh_get(table, db.tables, table->hash);

	db_table_handle_delete(table, k);
}


void db_entry_handle_delete(cache_entry* entry, khiter_t k){
	assert(!entry->deleted);

	if (IS_SINGLE_FILE(entry)){
		//Unlink
		get_key_path(entry, filename_buffer);
		unlink(filename_buffer);
	}

	//Counters
	db.db_size_bytes -= entry->data_length;
	db.db_keys--;

	//Remove from hash table
	kh_del(entry, entry->table->cache_hash_set, k);

	//Dont need the key any more, deleted
	entry->deleted = true;

	if (!entry->writing){
		//Remove from LRU
		db_lru_remove_node(entry);
	}

	//Assertion check
	assert(entry->refs != 0);

	//If table entry, cleanup table
	if (kh_size(entry->table->cache_hash_set) == 0){
		assert(!entry->table->deleted);
		entry->table->deleted = true;
		k = kh_get(table, db.tables, entry->table->hash);
		db_delete_table_entry(entry->table, k);
	}
}

void db_target_write_allocate(struct cache_target* target, uint32_t data_length){
	cache_entry* entry = target->entry;
	DEBUG("[#] Allocating space for entry, block is currently: %d and is single file: %d (was: %d)\n", entry->block, data_length > BLOCK_LENGTH, entry->block == -2?-1:(IS_SINGLE_FILE(entry)));
	if (entry->block == -2){
		//if this is a new entry, with nothing previously allocated.
		if (data_length <= BLOCK_LENGTH){
			entry->block = db_block_allocate_new();
		}
	}
	else if (data_length > BLOCK_LENGTH){
		//If this is to be an entry stored in a file
		if (!IS_SINGLE_FILE(entry)){
			//We are going to use a file, and the entry is currently a block
			db_block_free(entry->block);

			//No longer using a block
			entry->block = -1;
		}
	}
	else{
		//If this is to be an entry stored in a block
		if (IS_SINGLE_FILE(entry)){
			//We are going to store in a block, and the entry is currently a file
			get_key_path(entry, filename_buffer);

			//Delete single file, its not needed any more
			unlink(filename_buffer);

			//Allocate a block
			entry->block = db_block_allocate_new();
		}
		//Else: We are going to use a block, and the entry is currently a block
	}

	//Update database counters
	db.db_size_bytes += data_length;
	entry->data_length = data_length;

	db_target_open(target);

	if (IS_SINGLE_FILE(entry)){
		//Lengthen file to required size
		if (ftruncate(target->fd, data_length)<0){
			PWARN("File truncation failed (fd: %d, length: %d)", target->fd, data_length);
		}
	}
}
