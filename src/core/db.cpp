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
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include "db.h"
#include "debug.h"
#include "hash.h"
#include "settings.h"
#include "timer.h"

#ifdef DEBUG_BUILD
#include <set>
#endif

#define DEC2ALPH(x) ('A' + (x)%26)

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
	.blocks_free = 0,
	.db_size_bytes = 0,
	.db_keys = 0,
	.db_stats_inserts = 0,
	.db_stats_gets = 0,
	.db_stats_deletes = 0,
	.db_stats_operations = 0,
	.tables = NULL
};

pid_t current_flush = 0;

static pid_t db_index_flush(bool copyOnWrite = true);

//Buffers
static char filename_buffer[MAX_PATH];

db_details* db_get_details() {
	return &db;
}

#ifdef DEBUG_BUILD
void db_validate_lru_flags() {
	for (khiter_t k = kh_begin(db.tables); k != kh_end(db.tables); ++k) {
		if (kh_exist(db.tables, k)) {
			db_table* table = kh_val(db.tables, k);
			for (khiter_t ke = kh_begin(table->cache_hash_set); ke != kh_end(table->cache_hash_set); ++ke) {
				if (kh_exist(table->cache_hash_set, ke)) {
					cache_entry* entry = kh_val(table->cache_hash_set, ke);
					//Assert that this entry in in the LRU like it should be
					assert(entry->lru_found || entry->writing);
					entry->lru_found = false;
				}
			}
		}
	}
}
#endif

void db_validate_lru() {
#ifdef DEBUG_BUILD
	cache_entry* entry = db.lru_head;
	while (entry != NULL) {
		assert(!entry->lru_found);
		entry->lru_found = true;
		entry = entry->lru_next;
	}
	db_validate_lru_flags();

	entry = db.lru_tail;
	while (entry != NULL) {
		assert(!entry->lru_found);
		entry->lru_found = true;
		entry = entry->lru_prev;
	}
	db_validate_lru_flags();
#endif
}

void db_lru_remove_node(cache_entry* entry) {
	assert(!entry->lru_removed);
	if (entry->lru_prev != NULL) {
		assert(db.lru_head != entry);
		entry->lru_prev->lru_next = entry->lru_next;
	}
	else{
		//This node is the tail
		assert(db.lru_head == entry);
		db.lru_head = entry->lru_next;
	}

	if (entry->lru_next != NULL) {
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


void db_lru_insert(cache_entry* entry) {
	assert(entry->lru_removed);
	assert(entry != db.lru_tail);
	assert(entry->lru_next == NULL);
	assert(entry->lru_prev == NULL);

	//insert @ tail
	entry->lru_prev = db.lru_tail;
	if (db.lru_tail != NULL) {
		assert(db.lru_tail->lru_next == NULL);
		db.lru_tail->lru_next = entry;
	}
	entry->lru_next = NULL;
	db.lru_tail = entry;
	if (db.lru_head == NULL) {
		db.lru_head = entry;
	}

#ifdef DEBUG_BUILD
	entry->lru_removed = false;
#endif
}

void db_lru_hit(cache_entry* entry) {
	assert(!entry->lru_removed);

	//Remove from current position
	db_lru_remove_node(entry);

	//Re-insert @ tail
	db_lru_insert(entry);

	db_validate_lru();
}

static void db_block_size() {
	if (ftruncate(db.fd_blockfile, db.blocks_exist*BLOCK_LENGTH) < 0) {
		PWARN("File truncation failed (length: %d)", db.blocks_exist);
	}
}

void db_block_free(int32_t block) {
#ifdef DEBUG_BUILD
	std::set<uint32_t> block_check;
#endif
	block_free_node* old;
	assert(block + 1 <= db.blocks_exist);
	if (block + 1 == db.blocks_exist && db.blocks_free > 256) {
		db.blocks_exist--;
		db_block_size();
	}else{
		old = db.free_blocks;
#ifdef DEBUG_BUILD
		// Check only freed once
		block_free_node* ptr = db.free_blocks;
		while(ptr) {
			assert(block_check.insert(ptr->block_number).second);
			ptr = ptr->next;
		}
#endif
		db.free_blocks = (block_free_node*)malloc(sizeof(block_free_node));
		db.free_blocks->block_number = block;
		db.free_blocks->next = old;
		db.blocks_free++;
	}
}

int32_t db_block_allocate_new() {
	uint32_t block_num;
	block_free_node* free_node;
	if (db.free_blocks != NULL) {
		free_node = db.free_blocks;
		block_num = free_node->block_number;
		db.free_blocks = free_node->next;
		free(free_node);
		db.blocks_free--;
	}
	else if(db.blocks_exist >= 2147483647)
	{
		return -1; // too many
	}
	else
	{
		assert(db.blocks_free == 0);
		block_num = db.blocks_exist;
		db.blocks_exist++;
		db_block_size();
	}
	
	return block_num;
}

void db_entry_actually_delete(cache_entry* entry) {
	DEBUG("[#] Cleaning key up reference due to refcount == 0\n");
#ifdef DEBUG_BUILD
	assert(entry->lru_removed);
#endif
	//If is a block, can now free it
	if (!IS_SINGLE_FILE(entry) && entry->block >= 0) {
		db_block_free(entry->block);
		entry->block = -1;
	}

	//Free key
	free(entry->key);
	free(entry);
}

void db_table_actually_delete(db_table* entry) {
	DEBUG("[#] Cleaning table up reference due to refcount == 0\n");

	//Remove table from database
	khiter_t k = kh_get(table, db.tables, entry->hash);
	if (k != kh_end(db.tables)) {
		kh_del(table, db.tables, k);
	}

	//Free key
	free(entry->key);
	free(entry);
}

static void db_table_deref(db_table* entry, bool actually_delete = false) {
	DEBUG("[#] Decrementing table refcount - was: %d\n", entry->refs);
	assert(entry->refs > 0);
	entry->refs--;
	 
	//Actually clean up the entry
	if (entry->refs == 0 && (actually_delete || entry->deleted)) {
		//Remove table from hash set
		db_table_actually_delete(entry);
	}
}

void db_table_incref(db_table* entry) {
	DEBUG("[#] Incrementing table refcount - was: %d\n", entry->refs);
	entry->refs++;
}

void db_entry_deref(cache_entry* entry, bool table) {
	DEBUG("[#] Decrementing entry refcount - was: %d\n", entry->refs);
	entry->refs--;

	//Deref the table
	if (table && entry->table) {
		db_table_deref(entry->table);
	}

	//Actually clean up the entry
	if (entry->refs == 0 && entry->deleted) {
		db_entry_actually_delete(entry);
	}
}

void db_entry_incref(cache_entry* entry, bool table = true) {
	DEBUG("[#] Incrementing entry refcount - was: %d\n", entry->refs);
	entry->refs++;
	if (table)
		db_table_incref(entry->table);
}

void db_lru_cleanup_percent(int* bytes_to_remove) {
	int debug_bytes = *bytes_to_remove;
	while (db.lru_head != NULL && *bytes_to_remove > 0) {
		cache_entry* l = db.lru_head;

		//Skip if currently deleting
		if(l->writing || l->deleted) continue;

		*bytes_to_remove -= l->data_length;

		if (l->refs == 0)
		{
			db_entry_incref(l);
			db_entry_handle_delete(l);
			db_entry_deref(l);
		}
		else
		{
			db_entry_handle_delete(l);
		}
	}
	
	DEBUG("[#] LRU attempted to remove %d bytes, %d bytes remaining\n", debug_bytes, *bytes_to_remove);
}

static void force_link(const char* fileThatExists, const char* fileThatDoesNotExist){
	/*int ret = link(fileThatExists, fileThatDoesNotExist);
	char buffer[8096];
	if(ret != 0){
		PWARN("Unable to hard link");*/
		//Really bad! Temporary.
		snprintf(buffer, sizeof(buffer), "cp %s %s", fileThatExists, fileThatDoesNotExist);
		printf("Executing %s\n", buffer);
		system(buffer);
	//}
}

static int db_expire_cursor_table(db_table* table) {
	int ret = 0;

	for (khiter_t ke = kh_begin(table->cache_hash_set); ke < kh_end(table->cache_hash_set); ++ke) {
		if (kh_exist(table->cache_hash_set, ke)) {
			ret++;
			cache_entry* l = kh_value(table->cache_hash_set, ke);
			if (!l->deleted && l->expires != 0 && l->expires < time_seconds) {
				bool end_early = kh_size(table->cache_hash_set) == 1;
				if (l->refs == 0)
				{
					db_entry_incref(l);
					db_entry_handle_delete(l);
					db_entry_deref(l);
				}
				else
				{
					db_entry_handle_delete(l);
					end_early = false;
				}

				if (end_early) {
					break;
				}
			}
		}
	}
	return ret;
}

bool db_expire_cursor() {
	db_table* table;
	int done = 0;
	khiter_t start = db.table_gc;

	//Make sure tables havent been reduced
	if (start >= kh_end(db.tables)) {
		start = kh_begin(db.tables);
		db.table_gc = start;
	}

	//Empty table
	if (start == kh_end(db.tables)) {
		return true;
	}

	//Lets try and do atleast 1,000 keys, or the whole db (whichever first)
	do {
		if (kh_exist(db.tables, db.table_gc)) {
			table = kh_value(db.tables, db.table_gc);
			done += db_expire_cursor_table(table);
		}
		db.table_gc++;
		if (db.table_gc >= kh_end(db.tables)) {
			db.table_gc = kh_begin(db.tables);
		}
	} while (db.table_gc != start && done < 4096);
	
	return db.table_gc == start;
}

static bool currently_flushing(int flags){
	if(current_flush == 0) return false;
	int status, w;
	w = waitpid(current_flush, &status, flags);
	if (w) {
		if (WIFEXITED(status)) {
			current_flush = 0;
			return false;
		}
	} else if(w == -1) {
		current_flush = 0;
		return false;
	}
	return true;
}

void db_lru_gc() {
	if (settings.max_size > 0 && settings.max_size < db.db_size_bytes)
	{
		int bytes_to_remove;
		bool full_cycle;
		int i = 0;
		
		//Do 5 iterations, or one full cycle of expirations
		do
		{
			bytes_to_remove = (db.db_size_bytes - settings.max_size) + (settings.max_size * settings.db_lru_clear);
			full_cycle = db_expire_cursor();
			i++;
		} while (!full_cycle && i < 5);
		
		//Apply LRU
		bytes_to_remove = (db.db_size_bytes - settings.max_size) + (settings.max_size * settings.db_lru_clear);
		if (bytes_to_remove > 0) {
			db_lru_cleanup_percent(&bytes_to_remove);
		}
	}
	else
	{
		db_expire_cursor();
	}

	// Check for running flush
	if(currently_flushing(WNOHANG)) return;

	// Flush index
	pid_t pid = db_index_flush(true);
	if(pid == -1){
		PWARN("Unable to fork");
		return;
	}
	current_flush = pid;
}

static void db_clear_directory(const char* directory) {
	char file_buffer[MAX_PATH];
	struct dirent *next_file;
	DIR *theFolder = opendir(directory);
	while (next_file = readdir(theFolder))
	{
		if (next_file->d_name[0] == '.')
			continue;

		// build the full path for each file in the folder
		sprintf(file_buffer, "%s/%s", directory, next_file->d_name);
		unlink(file_buffer);
	}
	if (closedir(theFolder) < 0) {
		PFATAL("Unable to close directory.");
	}
}

void db_init_folders() {
	mkdir(db.path_single, 0777);

	for (char folder1 = 'A'; folder1 <= 'Z'; folder1++) {
		for (char folder2 = 'A'; folder2 <= 'Z'; folder2++) {
			snprintf(filename_buffer, MAX_PATH, "%s%c%c", db.path_single, folder1, folder2);

			if (access(filename_buffer, F_OK) == -1)
			{
				mkdir(filename_buffer, 0777);
			}
			else
			{
				db_clear_directory(filename_buffer);
			}
		}
	}
}

void db_complete_writing(cache_entry* entry) {
	assert(entry->writing);
	entry->writing = false;

	if (!entry->deleted) {
		//LRU: insert
		db_lru_insert(entry);
	}
}

static uint32_t hash_string(const char* str, int length) {
	uint32_t out;
	MurmurHash3_x86_32(str, length, HASH_SEED, &out);
	return out;
}

static void get_key_path(cache_entry* e, char* out) {
	snprintf(out, MAX_PATH, "%s%c%c/%x%x_%d", db.path_single, DEC2ALPH(e->hash), DEC2ALPH(e->hash >> 8), e->table->hash, e->hash, e->it);
}



cache_entry* db_entry_new(db_table* table) {
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

static void db_load_from_save_insert(db_table* table, cache_entry* entry){
	khiter_t k;
	db.db_keys++;
	//Must be checked before softdelete removes an entry being replaced
	bool is_first_in_table = kh_size(table->cache_hash_set) == 0;
	//Take a reference if this is the first entry (released when size == 0)
	if (is_first_in_table) {
		db_table_incref(table);
	}
	assert(table->refs >= 2 || (table->refs >= 1 && table->deleted)); //If not, it wouldnt be storing

	//Store entry
	int ret;
	k = kh_put(entry, table->cache_hash_set, entry->hash, &ret);
	kh_value(table->cache_hash_set, k) = entry;

	//Refs
	db_entry_incref(entry, false);

	assert(!entry->deleted);
}

static cache_entry* db_load_from_save_entry(db_table* table, char* key, uint16_t length, int32_t block, uint32_t data_length, time_t expires, uint16_t it){
	assert(table->refs >= 1); //If not, it wouldnt be existing

	uint32_t hash = hash_string(key, length);
	cache_entry* entry;

	entry = db_entry_new(table);
	entry->block = block;
	entry->data_length = data_length;
	entry->key = key;
	entry->key_length = length;
	entry->hash = hash;
	entry->expires = expires;
	entry->it = it;
	return entry;
}

static bool db_load_from_save(){
	char buffer[MAX_PATH], buffer2[2048];
	char *bp;
	bool ret = false;
	size_t len = 0;
	uint32_t u1, u2, u3, u4;
	db_table* table = NULL;
	cache_entry* entry;
	int d1;
	ssize_t read;

	snprintf(buffer, sizeof(buffer), "%s/index.save", db.path_root);
	int fd = open(buffer, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR);
	if (fd == -1){
		return false; //it's ok
	}

	snprintf(buffer, sizeof(buffer), "%s/blockfile.db.save", db.path_root);
	db.fd_blockfile = open(buffer, O_RDWR | O_LARGEFILE , S_IRUSR | S_IWUSR);
	if(db.fd_blockfile == -1){
		PWARN("Unable to open saved block file");
		close(fd);
		return ret;
	}
	off64_t size = lseek64(db.fd_blockfile, 0L, SEEK_END);
	db.blocks_exist = (uint32_t)(size / BLOCK_LENGTH);

	FILE* fp = fdopen(fd, "r");

	while ((read = getline(&bp, &len, fp)) != -1) {
        if(read <= 2 || bp[1] != ':') continue;
		switch(bp[0]){
			case 'f':
				if(sscanf(bp, "f:%u", &u1) != 1){
					WARN("Free block parsing error\n");
					goto free_loop;
				}
				db_block_free(u1);
			break;
			case 't':
				if(sscanf(bp, "t:%s", &buffer2) != 1){
					WARN("Table parsing error\n");
					goto free_loop;
				}
				if(table != NULL){
					db_table_deref(table, true);
					table = NULL;
				}
				table = db_table_get_write(strdup(buffer2), strlen(buffer2));
				entry = NULL;
				break;
			case 'b':
			case 'e':
				if(!table){
					WARN("File entry must be after table\n");
					goto free_loop;
				}
				if(sscanf(bp+1, ":%d:%u:%u:%u:%s", &d1, &u2, &u3, &u4, &buffer2) != 5){
					WARN("Entry parsing error\n");
					goto free_loop;
				}
			
				//>block, ce->data_length, ce->expires, ce->it
				entry = db_load_from_save_entry(table, strdup(buffer2), strlen(buffer2), d1, u2, u3, u4);

				if(d1 < 0){
					// Test file existance
					get_key_path(entry, buffer);
					
					if( access( buffer, F_OK ) == -1 ) {
						DEBUG("skipping as file %s does not exist\n", buffer);
						free(entry);
						goto free_loop;
					}
				}else{
					// Test size of blockfile
					if(d1 >= db.blocks_exist){
						DEBUG("skipping as block %d does not exist\n", d1);
						free(entry);
						goto free_loop;
					}
				}

				db_load_from_save_insert(table, entry);
				db_lru_insert(entry);

				break;
			default:
				printf("Unknown line type %c\n", bp[0]);
		}

free_loop:
		free(bp);
    }

	if(table != NULL){
		db_table_deref(table, true);
		table = NULL;
	}

	// move over block file (via link to preserve save)
	snprintf(buffer, sizeof(buffer), "%s/blockfile.db.save", db.path_root);
	unlink(db.path_blockfile);
	force_link(buffer, db.path_blockfile);

	ret = true;
close_fd:
	fclose(fp);
	close(db.fd_blockfile);

	close(fd);
	return ret;
}

bool db_open(const char* path) {
	bool will_black = false;

	//Create paths as char*'s
	snprintf(db.path_root, MAX_PATH, "%s/", path);
	snprintf(db.path_single, MAX_PATH, "%s/files/", path);

	//Initialize folder structure if it doesnt exist
	db_init_folders();

	// Initialize the tables hash
	db.tables = kh_init(table);
	db.table_gc = kh_begin(db.tables);

	//Load from index if available
	snprintf(db.path_blockfile, MAX_PATH, "%s/blockfile.db", path);
	
	if(!db_load_from_save()){
		PWARN("Unable to load index from disk, will blank database");
		will_black = true;
	}

	//Block file
	db.fd_blockfile = open(db.path_blockfile, O_CREAT | O_RDWR | O_LARGEFILE , S_IRUSR | S_IWUSR);
	if (db.fd_blockfile < 0) {
		PFATAL("Failed to open blockfile: %s", db.path_blockfile);
	}

	// Calculate the number of blocks in the blockfile
	off64_t size = lseek64(db.fd_blockfile, 0L, SEEK_END);
	db.blocks_exist = (uint32_t)(size / BLOCK_LENGTH);

	// Mark all blocks that already exist in the block file as non-allocated
	if(will_black){
		if(size > (BLOCK_MAX_LOAD * BLOCK_LENGTH)) {
			size = BLOCK_MAX_LOAD * BLOCK_LENGTH;
			ftruncate(db.fd_blockfile, size);
			db.blocks_exist = (uint32_t)(size / BLOCK_LENGTH);
		}
		for (off64_t i = 0; i < size; i += BLOCK_LENGTH) {
			db_block_free((uint32_t)(i / BLOCK_LENGTH));
		}
		db.blocks_free = db.blocks_exist;
	}

	return true;
}

int db_entry_open(struct cache_entry* e, mode_t modes) {
	get_key_path(e, filename_buffer);
	int fd = open(filename_buffer, O_RDWR | modes | O_LARGEFILE, S_IRUSR | S_IWUSR);
	return fd;
}

int db_entry_open_create(struct cache_entry* e) {
	int fd;
	
	e->it = 0;
	for(int i=0;i<65500;i++) {
		fd = db_entry_open(e, O_CREAT | O_EXCL);
		if(fd >= 0) {
			return fd;
		}
		e->it++;
		close(fd);
	}
	
	return -1;
}

void db_target_open(struct cache_target* target, bool write) {
	if (IS_SINGLE_FILE(target->entry)) {
		target->position = 0;
		if(write) {
			target->fd = db_entry_open_create(target->entry);
		}else{
			target->fd = db_entry_open(target->entry, 0);
		}
		if (target->fd <= 0) {
			WARN("Unable to open cache file: %d", target->entry);
		}
	}
	else{
		target->fd = db.fd_blockfile;
		target->position = ((off64_t)target->entry->block) * BLOCK_LENGTH;
	}
	target->end_position = target->position + target->entry->data_length;
}

void db_target_setup(struct cache_target* target, struct cache_entry* entry, bool write) {
	if (!write) {
		db_target_open(target, write);
	}
	else{
		target->fd = -1;
	}
}

void db_target_entry_close(cache_target* target) {
	if (target->entry != NULL) {
		if (target->fd != db.fd_blockfile && target->fd != -1) {
			assert(target->fd > 0 || settings.daemon_mode);
			close(target->fd);
		}

		db_entry_deref(target->entry);
		target->entry = NULL;
		target->fd = -1;
	}
	target->position = 0;
}

void db_table_close(db_table* table) {
	db_table_deref(table);
}

cache_entry* db_entry_get_read(struct db_table* table, char* key, size_t length) {
	uint32_t hash = hash_string(key, length);

	khiter_t k = kh_get(entry, table->cache_hash_set, hash);
	cache_entry* entry = k == kh_end(table->cache_hash_set) ? NULL : kh_value(table->cache_hash_set, k);

	if (entry == NULL) {
		DEBUG("[#] Key does not exist\n");
		free(key);
		return NULL;
	}
	assert(entry->hash == hash);

	if (entry->expires != 0) {
		DEBUG("[#] Key has ttl: %lu (%d from now)\n", (unsigned long)entry->expires, (int)(entry->expires - time_seconds));
	}

	if (entry->expires != 0 && entry->expires < time_seconds) {
		DEBUG("[#] Key expired\n");
		free(key);
		db_entry_incref(entry, false);
		db_entry_handle_delete(entry);
		db_entry_deref(entry, false);
		return NULL;
	}

	assert(!entry->deleted);

	if (entry->key_length != length || strncmp(key, entry->key, length) != 0) {
		DEBUG("[#] Unable to look up key: ");

		if (entry->key_length != length) {
			DEBUG("DB Key length does not match\n");
		}
		else{
			if (strncmp(key, entry->key, length) != 0) {
				DEBUG("String Keys dont match\n");
			}
		}

		free(key);
		return NULL;
	}

	//Free key text, not needed.
	free(key);

	//Stats
	db.db_stats_gets++;
	db.db_stats_operations++;

	//Check if currently writing (unfinished)
	if (entry->writing) {
		//TODO: possibly future, subscribe and writer handles data delivery
		return NULL;
	}

	//LRU hit
	db_lru_hit(entry);

	//Refs
	db_entry_incref(entry, false);

	return entry;
}

/**
Get table entry, if it does not exist return NULL. Will free name parameter
*/
struct db_table* db_table_get_read(char* name, int length) {
	uint32_t hash = hash_string(name, length);
	khiter_t k;
	db_table* table;
	bool while_condition;

	//Open addressing for table hash collision
	do
	{
		k = kh_get(table, db.tables, hash);
		if (k == kh_end(db.tables)) {
			table = NULL;
			goto end;
		}

		table = kh_value(db.tables, k);
		assert(table != NULL);
		assert(table->hash == hash);
		
		//Check table key, cache key collision handling
		while_condition = table->key_length != length || strncmp(table->key, name, length) != 0;
		if (while_condition)
		{
			hash++;
		}
	} while (while_condition);

	db_table_incref(table);

end:
	free(name);
	return table;
}

/**
Get Database table, if it does not exist - create.

This function will assume responsibility for freeing name if a table is found, else it will be used.

Returns NULL in case of cache collision
*/
struct db_table* db_table_get_write(char* name, int length) {
	uint32_t hash = hash_string(name, length);
	khiter_t k;
	db_table* table;
	int ret;
	bool while_condition;
	
	//Open addressing for table hash collision
	do
	{
		k = kh_get(table, db.tables, hash);
		//Create table if not exists
		if (k == kh_end(db.tables)) {
			table = (db_table*)malloc(sizeof(db_table));
			table->hash = hash;
			table->key = name;
			table->key_length = length;
			table->refs = 1;
			table->deleted = false;
			table->cache_hash_set = kh_init(entry);

			k = kh_put(table, db.tables, hash, &ret);
			kh_value(db.tables, k) = table;
			return table;
		}

		table = kh_value(db.tables, k);
		assert(table != NULL);
		assert(table->hash == hash);
	
		//Check table key, cache key collision handling
		while_condition = table->key_length != length || strncmp(table->key, name, length) != 0;
		if (while_condition)
		{
			hash++;
		}
	} while (while_condition);

	free(name);
	db_table_incref(table);
	
end:
	return table;
}

void db_entry_handle_softdelete(cache_entry* entry, khiter_t k) {
	//It is possible when over-writing for this to be called twice.
	if(entry->deleted) return;

	//If this is contained within a file, delete
	if (IS_SINGLE_FILE(entry)) {
		get_key_path(entry, filename_buffer);
		unlink(filename_buffer);
	}

	//Dont need the key any more, deleted
	entry->deleted = true;
	kh_del(entry, entry->table->cache_hash_set, k);

	//Counters
	db.db_size_bytes -= entry->data_length;

	//Remove from LRU, but only if not writing (as not, yet added)
	if (!entry->writing) {
		db_lru_remove_node(entry);
	}

	//Assertion check
	if (entry->refs == 0) {
		DEBUG("[#] Entry can be immediately cleaned up\n");
		db_entry_actually_delete(entry);
	}
}

/*
Get a Cache Entry to write
*/
cache_entry* db_entry_get_write(struct db_table* table, char* key, size_t length) {
	assert(table->refs >= 1); //If not, it wouldnt be existing

	uint32_t hash = hash_string(key, length);
	khiter_t k = kh_get(entry, table->cache_hash_set, hash);
	cache_entry* entry = k == kh_end(table->cache_hash_set) ? NULL : kh_value(table->cache_hash_set, k);

	//Stats
	db.db_stats_inserts++;
	db.db_stats_operations++;

	//Must be checked before softdelete removes an entry being replaced
	bool is_first_in_table = kh_size(table->cache_hash_set) == 0;

	//This is a re-used entry
	if (entry != NULL)
	{
		assert(entry->hash == hash);
		//If we are currently writing, then it will be mocked
		if (entry->writing == true) {
			return NULL;
		}

		//We might have clients reading this key
		db_entry_handle_softdelete(entry, k);

		db_validate_lru();
	}
	else
	{
		db.db_keys++;
	}
	
	int32_t block = db_block_allocate_new();
	if(block == -1) {
		return NULL;
	}

	entry = db_entry_new(table);
	entry->block = block;
	entry->data_length = 0;
	entry->key = key;
	entry->key_length = length;
	entry->hash = hash;

	//Take a reference if this is the first entry (released when size == 0)
	if (is_first_in_table) {
		db_table_incref(table);
	}
	assert(table->refs >= 2 || (table->refs >= 1 && table->deleted)); //If not, it wouldnt be storing

	//Store entry
	int ret;
	k = kh_put(entry, table->cache_hash_set, entry->hash, &ret);
	kh_value(table->cache_hash_set, k) = entry;

	//Refs
	db_entry_incref(entry, false);
	entry->writing = true;

	assert(!entry->deleted);

	//LRU
	if ((db.db_stats_inserts % DB_LRU_EVERY) == 0) {
		DEBUG("[#] Do LRU GC check.\n");
		db_lru_gc();
	}

	return entry;
}

/*
Get a Cache Entry to delete
*/
cache_entry* db_entry_get_delete(struct db_table* table, char* key, size_t length) {
	uint32_t hash = hash_string(key, length);
	khiter_t k = kh_get(entry, table->cache_hash_set, hash);
	cache_entry* entry = k == kh_end(table->cache_hash_set) ? NULL : kh_value(table->cache_hash_set, k);

	if (entry == NULL || entry->key_length != length || strncmp(key, entry->key, length) != 0) {
		DEBUG("[#] Unable to look up key: ");

		if (entry == NULL) {
			DEBUG("DB Key is null\n");
		}
		else{
			if (entry->key_length != length) {
				DEBUG("DB Key length does not match\n");
			}
			else{
				if (strncmp(key, entry->key, length)) {
					DEBUG("String Keys dont match\n");
				}
			}
		}

		free(key);
		return NULL;
	}
	assert(entry->hash == hash);

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

#ifdef DEBUG_BUILD
/*
DEBUG ONLY: Test the refcounts of each table, expects nothing to be open (refs==1)
*/
void db_check_table_refs() {
	db_table* table;

	for (khiter_t ke = kh_begin(db.tables); ke != kh_end(db.tables); ++ke) {
		if (kh_exist(db.tables, ke)) {
			table = kh_val(db.tables, ke);

			//All other refernces should have been de-refed before db_close is called
			//and hence anything pending deletion will have been cleaned up already
			assert(!table->deleted);

			//Check reference count (should be 1)
			assert(table->refs == 1);
		}
	}
}
#endif

static void db_entry_cleanup(cache_entry* entry) {
	if (IS_SINGLE_FILE(entry)) {
		get_key_path(entry, filename_buffer);
		unlink(filename_buffer);
	}
}

bool db_entry_handle_delete(cache_entry* entry) {
	khiter_t k = kh_get(entry, entry->table->cache_hash_set, entry->hash);

	return db_entry_handle_delete(entry, k);
}

void db_delete_table_entry(db_table* table, khiter_t k) {
	kh_destroy(entry, table->cache_hash_set);

	//If not fully de-refed remove now, not later
	if (table->refs != 0) {
		assert(k != kh_end(db.tables));
		kh_del(table, db.tables, k);
	}

	//Remove reference holding table open
	db_table_deref(table);
}


void db_table_handle_delete(db_table* table, khiter_t k) {
	//Set deleted
	assert(!table->deleted);
	table->deleted = true;

	//Delete keys from table
	for (khiter_t ke = kh_begin(table->cache_hash_set); ke != kh_end(table->cache_hash_set); ++ke) {
		if (kh_exist(table->cache_hash_set, ke)) {
			cache_entry* ce = kh_val(table->cache_hash_set, ke);
			if (!ce->deleted) {
				db_entry_handle_softdelete(ce, ke);
				db_entry_cleanup(ce);
			}
		}
	}
	
	db_delete_table_entry(table, k);
}


void db_table_handle_delete(db_table* table) {
	khiter_t k = kh_get(table, db.tables, table->hash);

	return db_table_handle_delete(table, k);
}

bool db_entry_handle_delete(cache_entry* entry, khiter_t k) {
	assert(!entry->deleted);

	db_entry_cleanup(entry);

	//Counters
	db.db_size_bytes -= entry->data_length;
	db.db_keys--;

	//Remove from hash table
	kh_del(entry, entry->table->cache_hash_set, k);

	//Dont need the key any more, deleted
	entry->deleted = true;

	if (!entry->writing) {
		//Remove from LRU
		db_lru_remove_node(entry);
	}

	//Assertion check
	assert(entry->refs != 0);

	//If table entry, cleanup table
	if (kh_size(entry->table->cache_hash_set) == 0) {
		assert(!entry->table->deleted);
		entry->table->deleted = true;
		k = kh_get(table, db.tables, entry->table->hash);
		db_delete_table_entry(entry->table, k);
		entry->table = NULL;
		return true;
	}
	return false;
}

void db_target_write_allocate(struct cache_target* target, uint32_t data_length) {
	cache_entry* entry = target->entry;
	DEBUG("[#] Allocating space for entry, block is currently: %d and is single file: %s (was: %s)\n", entry->block, (data_length > BLOCK_LENGTH) ? "yes" : "no", (entry->block == -2?-1:(IS_SINGLE_FILE(entry)))? "yes":"no");
	if (entry->block == -2) {
		//if this is a new entry, with nothing previously allocated.
		if (data_length <= BLOCK_LENGTH) {
			entry->block = db_block_allocate_new();
		}
	}
	else if (data_length > BLOCK_LENGTH) {
		//If this is to be an entry stored in a file
		if (!IS_SINGLE_FILE(entry) && entry->block >= 0) {
			//We are going to use a file, and the entry is currently a block
			db_block_free(entry->block);

			//No longer using a block
			entry->block = -1;
		}
	}
	else{
		//If this is to be an entry stored in a block
		if (IS_SINGLE_FILE(entry)) {
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
	if (entry->block == -2)
	{
		db.db_size_bytes += data_length;
	}
	else
	{
		db.db_size_bytes += data_length - entry->data_length;
	}
	
	entry->data_length = data_length;

	db_target_open(target, true);

	if (IS_SINGLE_FILE(entry)) {
		//Lengthen file to required size
		if (ftruncate(target->fd, data_length)<0) {
			PWARN("File truncation failed (fd: %d, length: %d)", target->fd, data_length);
		}
	}
}

static void db_close_table_key_space() {
	db_table* table;

	//make 128 attempts to clear the tablespace
	//table deletions can cause resizing and tables to be skipped in the iteration (todo: really?)
	for (int i = 0; i < 128 && kh_size(db.tables); i++) {
		for (khiter_t ke = kh_begin(db.tables); ke < kh_end(db.tables); ++ke) {
			if (kh_exist(db.tables, ke)) {
				table = kh_val(db.tables, ke);

				//All other refernces should have been de-refed before db_close is called
				//and hence anything pending deletion will have been cleaned up already
				assert(!table->deleted);

				//Check reference count (should be 1)
				assert(table->refs == 1);

				//Actually delete
				db_table_handle_delete(table);
			}
		}
	}
	kh_destroy(table, db.tables);
}

static void db_close_blockfile() {
	block_free_node* bf = db.free_blocks;
	block_free_node* bf2;
	while (bf != NULL) {
		bf2 = bf;
		bf = bf->next;
		free(bf2);
	}
	db.free_blocks = NULL;
}

static bool full_write(int fd, char* buffer, int buffer_length){
	int ret;
	do {
		ret = write(fd, buffer, buffer_length);
		if(ret == -1){
			return false;
		}
		buffer_length -= ret;
		buffer += ret;
	} while(buffer_length > 0);
	return true;
}

static pid_t db_index_flush(bool copyOnWrite){
	pid_t pid = 0;
	char buffer[2048], buffer2[2048], buffer3[2048], buffer4[2048];
	db_table* table;
	cache_entry* ce;
	int temp;

	//Create hardlink to blockfile
	snprintf(buffer, sizeof(buffer), "%s.temp", db.path_blockfile);
	unlink(buffer);
	force_link(db.path_blockfile, buffer);

	//If we are forking we can do so now
	if(copyOnWrite){
		pid = fork();
		if(pid != 0) return pid; // includes -1
	}

	//Open temporary index file
	snprintf(buffer, sizeof(buffer), "%s/db.temp", db.path_root);
	int fd = open(buffer, O_RDWR | O_CREAT | O_TRUNC | O_LARGEFILE, S_IRUSR | S_IWUSR);
	if(fd == -1){
		PWARN("Unable to flush index, unable to open file");
		return false;
	}

	//Write free blocks
	block_free_node *free_node = db.free_blocks;
	while(free_node != NULL){
		temp = snprintf(buffer, sizeof(buffer), "f:%u\n", free_node->block_number);
		if(!full_write(fd, buffer, temp)) goto close_fd;
		free_node = free_node->next;
	}

	//Write tables and cache entries
	for (khiter_t ke = kh_begin(db.tables); ke < kh_end(db.tables); ++ke) {
		if (kh_exist(db.tables, ke)) {
			//Write table to index
			table = kh_val(db.tables, ke);
			if(!full_write(fd, "t:", 2)) goto close_fd;
			if(!full_write(fd, table->key, table->key_length)) goto close_fd;
			if(!full_write(fd, "\n", 1)) goto close_fd;

			//Iterate entries in table
			for (khiter_t kee = kh_begin(table->cache_hash_set); kee != kh_end(table->cache_hash_set); ++kee) {
				if (kh_exist(table->cache_hash_set, kee)) {
					ce = kh_val(table->cache_hash_set, kee);
					if(ce->writing || ce->deleted) continue;

					//Write entry key to index
					temp = snprintf(buffer, sizeof(buffer), "%s:%d:%u:%u:%u:", ce->block >= 0 ? "b":"e", ce->block, ce->data_length, ce->expires, ce->it);
					if(!full_write(fd, buffer, temp)) goto close_fd;
					if(!full_write(fd, ce->key, ce->key_length)) goto close_fd;
					if(!full_write(fd, "\n", 1)) goto close_fd;
				}
			}
		}
	}

	close(fd);

	// db.temp -> db.index & blockfile.temp -> blockfile.save
	snprintf(buffer, sizeof(buffer), "%s/db.temp", db.path_root);
	snprintf(buffer2, sizeof(buffer2), "%s/index.save", db.path_root);
	unlink(buffer2);
	snprintf(buffer3, sizeof(buffer3), "%s.temp", db.path_blockfile);
	snprintf(buffer4, sizeof(buffer4), "%s.save", db.path_blockfile);
	rename(buffer, buffer2);
	rename(buffer3, buffer4);
	unlink(buffer3);
	unlink(buffer);

	pid = 0;
	
	close_fd:
	if(fd != -1) {
		close(fd);
	}

	if(copyOnWrite){
		if(pid){
			PWARN("Unable to write index due to error");
			
			snprintf(buffer2, 1024, "%s/db.temp", db.path_root);
			unlink(buffer2);
		}
		exit(0);
	}

	return pid;
}

/*
Close the database engine
*/
void db_close() {
	currently_flushing(0);
	db_index_flush(false);
	db_close_table_key_space();
	db_close_blockfile();
}
