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
state_action http_read_handle_state(int epfd, cache_connection* connection){
	return connection->handler(epfd, connection);
}

/*
Handle the connection (Read Event)

return true to close the connection
*/
state_action http_read_handle(int epfd, cache_connection* connection){
	int num;
	int fd = connection->client_sock;

	//Read from socket
	num = rbuf_write_to_end(&connection->input);
	if (num > 0){
		num = read(fd, RBUF_WRITE(connection->input), num);

		if (num <= 0){
			if (errno != EAGAIN && errno != EWOULDBLOCK){
				DEBUG("A socket error occured while reading: %d", num);
				return close_connection;
			}
			else{
				return needs_more;
			}
		}

		RBUF_WRITEMOVE(connection->input, num);
	}

	state_action run;
	do {
		run = http_read_handle_state(epfd, connection);
	} while (run == needs_more && rbuf_read_to_end(&connection->input) != 0);

	//Handle buffer is full, not being processed
	if (num == 0 && rbuf_write_remaining(&connection->input) == 0){
		WARN("Buffer full, not processed, disconnecting.");
		return close_connection;
	}

	return run;
}

/*
Handle the writing of data to the connection

return true to signal intent to send more data
*/
state_action http_write_handle_state(int epfd, cache_connection* connection){
	return connection->handler(epfd, connection);
}

/*
Handle the connection (Write Event)

return true to close the connection
*/
state_action http_write_handle(int epfd, cache_connection* connection){
	if (connection->output_buffer != NULL){
		//Send data
		int num = write(connection->client_sock, connection->output_buffer, connection->output_length);
		if (num < 0){
			//TODO: handle error
			return close_connection;
		}
		connection->output_length -= num;

		//Check if done
		if (connection->output_length == 0){
			//If done, null output buffer
			connection->output_buffer = NULL;

			//if need free, free and null
			if (connection->output_buffer_free){
				free(connection->output_buffer_free);
				connection->output_buffer_free = NULL;
			}
		}
	}

	state_action run = continue_processing;
	if (connection->output_buffer == NULL){
		do {
			run = http_write_handle_state(epfd, connection);
		} while (run == needs_more);
	}

	return run;
}

void http_cleanup(cache_connection* connection){
	if (REQUEST_IS(connection->type, REQUEST_LEVELKEY)){
		if (connection->writing){
			cache_entry* entry = connection->target.key.entry;
			entry->writing = false;
			db_entry_handle_delete(entry);
		}
		if (connection->target.key.entry != NULL){
			db_target_close(&connection->target.key);
		}
	}
}

/*
Initialize the http_templates_length structure with the length of the
static http_templates.
*/
void http_templates_init(){
	for (int i = 0; i < NUMBER_OF_HTTPTEMPLATE; i++){
		http_templates_length[i] = strlen(http_templates[i]);
	}
}

void http_connection_handler(cache_connection* connection){
	connection->handler = http_handle_method;
	connection->state = 0;
}