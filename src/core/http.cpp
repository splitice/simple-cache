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
#include "http.h"
#include "config.h"
#include "debug.h"
#include "connection.h"
#include "db.h"
#include "timer.h"
#include "http_parse.h"

int http_templates_length[NUMBER_OF_HTTPTEMPLATE];

/*
Handle read data for the connection

return true to signal intent to process more data
*/
state_action http_read_handle_state(int epfd, cache_connection* connection) {
	return connection->handler(epfd, connection);
}

/*
Handle the connection (Read Event)

return true to close the connection
*/
state_action http_read_handle(int epfd, cache_connection* connection) {
	int num;
	int fd = connection->client_sock;

	//Optimization
	if (connection->input.write_position == connection->input.read_position) {
		connection->input.write_position = connection->input.read_position = 0;
	}

	//Read from socket
	num = rbuf_write_to_end(&connection->input);
	if (num > 0) {
		DEBUG("[#%d] reading %d bytes from socket (write pos: %d, read pos: %d)\n", fd, num, connection->input.write_position, connection->input.read_position);
		num = read(fd, RBUF_WRITE(connection->input), num);

		if (num == 0) {
			return close_connection;
		}
		if (num < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				DEBUG("A socket error occured while reading: %d", num);
				return close_connection;
			}
			else{
				return needs_more;
			}
		}

		RBUF_WRITEMOVE(connection->input, (uint16_t)num);
	}

	state_action run;
	int to_end = -1, to_end_old;
	do {
		run = http_read_handle_state(epfd, connection);
		to_end_old = to_end;
		to_end = rbuf_read_remaining(&connection->input);
	} while (run == needs_more && to_end != 0 && to_end != to_end_old);

	//Handle buffer is full, not being processed
	if (num == 0 && rbuf_write_remaining(&connection->input) == 0) {
		WARN("Buffer full, not processed, disconnecting.");
		return close_connection;
	}

	return run;
}

/*
Handle the writing of data to the connection

return true to signal intent to send more data
*/
state_action http_write_handle_state(int epfd, cache_connection* connection) {
	return connection->handler(epfd, connection);
}

/*
Handle the connection (Write Event)

return true to close the connection
*/
state_action http_write_handle(int epfd, cache_connection* connection) {
	if (connection->output_buffer != NULL) {
		//Send data
		int num = write(connection->client_sock, connection->output_buffer, connection->output_length);
		if (num < 0) {
			//TODO: handle error
			return close_connection;
		}
		connection->output_length -= num;

		//Check if done
		if (connection->output_length == 0) {
			//If done, null output buffer
			connection->output_buffer = NULL;

			//if need free, free and null
			if (connection->output_buffer_free) {
				free(connection->output_buffer_free);
				connection->output_buffer_free = NULL;
			}
		}
	}

	state_action run = continue_processing;
	if (connection->output_buffer == NULL) {
		do {
			run = http_write_handle_state(epfd, connection);
		} while (run == needs_more);
	}

	return run;
}

/*
Cleanup & Reset state for a HTTP connection
*/
void http_cleanup(cache_connection* connection) {
	assert(connection != NULL);
	DEBUG("[#%d] Cleaning up connection\n", connection->client_sock);
	if (REQUEST_IS(connection->type, REQUEST_LEVELKEY)) {
		if (connection->writing) {
			cache_entry* entry = connection->target.key.entry;
			assert(entry != NULL);
			if (!entry->deleted) {
				db_entry_handle_delete(entry);
			}
			entry->writing = false;
			connection->writing = false;
		}
		if (connection->target.key.entry != NULL) {
			db_target_entry_close(&connection->target.key);
			assert(connection->target.key.entry == NULL);
		}
	}
	else if(REQUEST_IS(connection->type, REQUEST_LEVELTABLE)) {
		db_table* table = connection->target.table.table;
		if (table != NULL) {
			db_table_close(table);
			connection->target.table.table = NULL;
		}
	}
	connection->type = 0;
}

/*
Initialize the http_templates_length structure with the length of the
static http_templates.
*/
void http_templates_init() {
	for (int i = 0; i < NUMBER_OF_HTTPTEMPLATE; i++) {
		http_templates_length[i] = strlen(http_templates[i]);
	}
}

void http_connection_handler(cache_connection* connection) {
	CONNECTION_HANDLER(connection,  http_handle_method);
	connection->state = 0;
}