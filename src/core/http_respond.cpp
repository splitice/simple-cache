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
#include "http_respond.h"

state_action http_respond_contentlength(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	char content_length_buffer[128];
	DEBUG("[#%d] Responding with Content-Length\n", fd);
	//Returns the number of chars put into the buffer
	int temp = snprintf(content_length_buffer, 128, "Content-Length: %d\r\n", connection->target.key.entry->data_length);

	connection->output_buffer_free = (char*)malloc(temp);
	memcpy(connection->output_buffer_free, content_length_buffer, temp);
	connection->output_buffer = connection->output_buffer_free;
	connection->output_length = temp;
	connection->handler = http_respond_responseend;
	return continue_processing;
}

state_action http_respond_responseend(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	DEBUG("[#%d] Responding with the newlines\n", fd);
	connection->output_buffer = http_templates[HTTPTEMPLATE_NEWLINE];
	connection->output_length = http_templates_length[HTTPTEMPLATE_NEWLINE];
	connection->handler = http_respond_writeonly;
	return continue_processing;
}

state_action http_respond_contentbody(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	DEBUG("[#%d] Sending response body\n", fd);
	//The number of bytes to read
	int temp = connection->target.key.end_position - connection->target.key.position;
	DEBUG("[#%d] To send %d bytes to the socket (len: %d, pos: %d)\n", fd, temp, connection->target.key.entry->data_length, connection->target.key.position);
	assert(temp >= 0);
	if (temp != 0){
		off_t pos = connection->target.key.position;
		int bytes_sent = sendfile(fd, connection->target.key.fd, &pos, temp);
		if (bytes_sent < 0){
			PFATAL("Error sending bytes with sendfile");
		}
		DEBUG("[#%d] Sendfile sent %d bytes from position %d\n", fd, bytes_sent, connection->target.key.position);
		connection->target.key.position += bytes_sent;
		DEBUG("[#%d] Position is now %d\n", fd, connection->target.key.position);
	}

	assert(connection->target.key.position <= connection->target.key.end_position);
	if (connection->target.key.position == connection->target.key.end_position){
		db_target_close(&connection->target.key);
		connection->handler = http_handle_method;
		connection_register_read(epfd, fd);
	}
	return continue_processing;
}

state_action http_respond_writeonly(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	DEBUG("[#%d] Sending static response\n", fd);
	//Static response, after witing, read next request
	connection->handler = http_handle_method;
	connection_register_read(epfd, fd);
	return continue_processing;
}