#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <assert.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "config.h"
#include "debug.h"
#include "connection.h"
#include "hash.h"
#include "http.h"
#include "db.h"
#include "settings.h"
#include "timer.h"

void temporary_init(){
	struct cache_target target;

	char* tableKey = (char*)malloc(2);
	tableKey[0] = 't';
	tableKey[1] = '\0';

	struct db_table* table = db_table_get_write(tableKey, 1);

	target.position = 0;
	char* key = (char*)malloc(2);
	key[0] = 'e';
	key[1] = '\0';
	target.entry = db_entry_get_write(table, key, 1);
	target.fd = db_entry_open(target.entry, O_CREAT);
	target.fd = db.fd_blockfile;
	target.position = target.entry->block * BLOCK_LENGTH;

	db_entry_write_init(&target, 2);
	target.end_position = target.position + target.entry->data_length;
	lseek(target.fd, target.position, SEEK_SET);
	int written = write(target.fd, "OK", 2);
	if (written < 0){
		PFATAL("Error writing data");
	}
	target.entry->writing = false;
	db_entry_close(&target);
}

/* Time to go down the rabbit hole */
int main(int argc, char** argv)
{
	timer_setup();
	parse_arguments(argc, argv);
	http_templates_init();
	db_open("/dbtest");
	temporary_init();
	connection_open_listener();
	epoll_event_loop();
}