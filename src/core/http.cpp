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
bool http_read_handle_state(int epfd, cache_connection* connection){
	return connection->handler(epfd, connection);
}

/*
Handle the connection (Read Event)

return true to close the connection
*/
bool http_read_handle(int epfd, cache_connection* connection){
	int num;
	int fd = connection->client_sock;

	//Read from socket
	num = rbuf_write_to_end(&connection->input);
	assert(num > 0);
	num = read(fd, RBUF_WRITE(connection->input), num);

	if (num <= 0){
		DEBUG("A socket error occured while reading: %d", num);
		return true;
	}

	RBUF_WRITEMOVE(connection->input, num);

	bool run;
	do {
		run = http_read_handle_state(epfd, connection);
	} while (run);

	//TODO: Handle full buffer condition

	return false;
}

/*
Handle the writing of data to the connection

return true to signal intent to send more data
*/
bool http_write_handle_state(int epfd, cache_connection* connection){
	return connection->handler(epfd, connection);
}

/*
Handle the connection (Write Event)

return true to close the connection
*/
bool http_write_handle(int epfd, cache_connection* connection){
	if (connection->output_buffer != NULL){
		//Send data
		int num = write(connection->client_sock, connection->output_buffer, connection->output_length);
		if (num < 0){
			//TODO: handle error
			return true;
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

	if (connection->output_buffer == NULL){
		bool run;
		do {
			run = http_write_handle_state(epfd, connection);
		} while (run);
	}

	return false;
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