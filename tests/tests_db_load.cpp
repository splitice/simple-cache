#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include "minunit.h"
#include "../src/core/db.h"
#include "../src/core/db_structures.h"
#include "../src/core/config.h"

static char test_dir[MAX_PATH];
static char index_save_path[MAX_PATH];
static char blockfile_path[MAX_PATH];

static void setup_test_dir() {
    static int counter = 0;
    snprintf(test_dir, sizeof(test_dir), "/tmp/test_db_load_%d", counter++);
    mkdir(test_dir, 0755);
    snprintf(index_save_path, sizeof(index_save_path), "%s/index.save", test_dir);
    snprintf(blockfile_path, sizeof(blockfile_path), "%s/blockfile.db", test_dir);
}

static void cleanup_test_dir() {
    // Remove test files
    unlink(index_save_path);
    unlink(blockfile_path);
    
    // Remove any temp files that might have been created
    char temp_path[MAX_PATH];
    snprintf(temp_path, sizeof(temp_path), "%s/index.temp", test_dir);
    unlink(temp_path);
    
    rmdir(test_dir);
}

static bool write_file(const char* path, const char* content) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return false;
    if (write(fd, content, strlen(content)) < 0) { close(fd); return false; }
    close(fd);
    return true;
}

static bool create_blockfile(int num_blocks) {
    int fd = open(blockfile_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return false;
    if (ftruncate(fd, (off64_t)num_blocks * BLOCK_LENGTH) < 0) { close(fd); return false; }
    close(fd);
    return true;
}

static bool run_db_open() {
    char path_no_slash[MAX_PATH];
    snprintf(path_no_slash, sizeof(path_no_slash), "%s", test_dir);
    int len = (int)strlen(path_no_slash);
    if (len > 0 && path_no_slash[len-1] == '/') {
        path_no_slash[len-1] = '\0';
    }
    return db_open(path_no_slash);
}

/* GOOD CASES */

static const char * test_empty_dir() {
    setup_test_dir();
    bool result = run_db_open();
    mu_assert("empty_dir: db_open should succeed", result == true);
    db_details* details = db_get_details();
    mu_assert("empty_dir: tables empty", kh_size(details->tables) == 0);
    mu_assert("empty_dir: db_keys 0", details->db_keys == 0);
    mu_assert("empty_dir: db_size_bytes 0", details->db_size_bytes == 0);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_one_block_entry() {
    setup_test_dir();
    mu_assert("one_block: create blockfile", create_blockfile(2));
    const char* index_content = "t:test_table\nb:0:100:0:0:key1\n";
    mu_assert("one_block: write index", write_file(index_save_path, index_content));
    mu_assert("one_block: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("one_block: 1 table", kh_size(details->tables) == 1);
    mu_assert("one_block: 1 key", details->db_keys == 1);
    mu_assert("one_block: 100 bytes", details->db_size_bytes == 100);
    mu_assert("one_block: blocks_exist 2", details->blocks_exist == 2);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_multiple_entries_free_reconstruction() {
    setup_test_dir();
    mu_assert("multi_entry: create blockfile", create_blockfile(5));
    const char* index_content = "t:table1\nb:0:50:0:0:key1\nb:2:75:0:0:key2\nb:4:100:0:0:key3\n";
    mu_assert("multi_entry: write index", write_file(index_save_path, index_content));
    mu_assert("multi_entry: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("multi_entry: 1 table", kh_size(details->tables) == 1);
    mu_assert("multi_entry: 3 keys", details->db_keys == 3);
    mu_assert("multi_entry: 225 bytes", details->db_size_bytes == 225);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_old_format_with_f_lines() {
    setup_test_dir();
    mu_assert("old_fmt: create blockfile", create_blockfile(3));
    const char* index_content = "f:0\nf:1\nt:table1\nb:0:50:0:0:key1\nf:2\n";
    mu_assert("old_fmt: write index", write_file(index_save_path, index_content));
    mu_assert("old_fmt: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("old_fmt: 1 table", kh_size(details->tables) == 1);
    mu_assert("old_fmt: 1 key", details->db_keys == 1);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_multiple_tables() {
    setup_test_dir();
    mu_assert("multi_tbl: create blockfile", create_blockfile(10));
    const char* index_content = "t:table1\nb:0:100:0:0:key1\nb:1:200:0:0:key2\nt:table2\nb:2:150:0:0:key3\nb:3:300:0:0:key4\n";
    mu_assert("multi_tbl: write index", write_file(index_save_path, index_content));
    mu_assert("multi_tbl: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("multi_tbl: 2 tables", kh_size(details->tables) == 2);
    mu_assert("multi_tbl: 4 keys", details->db_keys == 4);
    mu_assert("multi_tbl: 750 bytes", details->db_size_bytes == 750);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_entry_with_expiry() {
    setup_test_dir();
    mu_assert("expiry: create blockfile", create_blockfile(1));
    time_t future_time = time(NULL) + 3600;
    char index_content[256];
    snprintf(index_content, sizeof(index_content), "t:table1\nb:0:100:%lu:0:key1\n", future_time);
    mu_assert("expiry: write index", write_file(index_save_path, index_content));
    mu_assert("expiry: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("expiry: 1 table", kh_size(details->tables) == 1);
    mu_assert("expiry: 1 key", details->db_keys == 1);
    db_close();
    cleanup_test_dir();
    return 0;
}

/* ERROR CASES */

static const char * test_malformed_line_no_colon() {
    setup_test_dir();
    mu_assert("malformed: create blockfile", create_blockfile(1));
    const char* index_content = "t:table1\nTHIS_IS_NOT_VALID\nb:0:100:0:0:key1\n";
    mu_assert("malformed: write index", write_file(index_save_path, index_content));
    mu_assert("malformed: db_open should not crash", run_db_open() == true);
    db_details* details = db_get_details();
    mu_assert("malformed: 1 table (skip bad line)", kh_size(details->tables) == 1);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_entry_nonexistent_block() {
    setup_test_dir();
    mu_assert("no_block: create blockfile", create_blockfile(1)); // only block 0
    const char* index_content = "t:table1\nb:5:100:0:0:key1\n"; // block 5 doesn't exist
    mu_assert("no_block: write index", write_file(index_save_path, index_content));
    mu_assert("no_block: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("no_block: 0 keys (entry skipped)", details->db_keys == 0);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_corrupt_entry_line() {
    setup_test_dir();
    mu_assert("corrupt: create blockfile", create_blockfile(2));
    const char* index_content = "t:table1\nb:not_a_number:100:0:0:key1\n";
    mu_assert("corrupt: write index", write_file(index_save_path, index_content));
    mu_assert("corrupt: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("corrupt: 0 keys (corrupt entry skipped)", details->db_keys == 0);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_entry_before_table() {
    setup_test_dir();
    mu_assert("before_tbl: create blockfile", create_blockfile(1));
    const char* index_content = "b:0:100:0:0:key1\nt:table1\nb:0:100:0:0:key2\n";
    mu_assert("before_tbl: write index", write_file(index_save_path, index_content));
    mu_assert("before_tbl: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("before_tbl: 1 key (first entry skipped)", details->db_keys == 1);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_empty_index_save() {
    setup_test_dir();
    mu_assert("empty_idx: create blockfile", create_blockfile(1));
    mu_assert("empty_idx: write index", write_file(index_save_path, "\n\n"));
    mu_assert("empty_idx: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("empty_idx: 0 tables", kh_size(details->tables) == 0);
    db_close();
    cleanup_test_dir();
    return 0;
}

static const char * test_empty_blockfile() {
    setup_test_dir();
    int fd = open(blockfile_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    mu_assert("empty_bf: create empty blockfile", fd >= 0);
    close(fd);
    const char* index_content = "t:table1\n";
    mu_assert("empty_bf: write index", write_file(index_save_path, index_content));
    mu_assert("empty_bf: db_open", run_db_open());
    db_details* details = db_get_details();
    mu_assert("empty_bf: blocks_exist 0", details->blocks_exist == 0);
    db_close();
    cleanup_test_dir();
    return 0;
}

const char* test_db_load_all() {
    mu_run_test(test_empty_dir);
    mu_run_test(test_one_block_entry);
    mu_run_test(test_multiple_entries_free_reconstruction);
    mu_run_test(test_old_format_with_f_lines);
    mu_run_test(test_multiple_tables);
    mu_run_test(test_entry_with_expiry);
    mu_run_test(test_malformed_line_no_colon);
    mu_run_test(test_entry_nonexistent_block);
    mu_run_test(test_corrupt_entry_line);
    mu_run_test(test_entry_before_table);
    mu_run_test(test_empty_index_save);
    mu_run_test(test_empty_blockfile);
    return 0;
}
