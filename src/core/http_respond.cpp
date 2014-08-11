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

void copy_to_outputbuffer(cache_connection* connection, char* header, int length){
	connection->output_buffer = connection->output_buffer_free = (char*)malloc(length);
	memcpy(connection->output_buffer_free, header, length);
	connection->output_length = length;
}

state_action http_respond_expires(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	char header[32];

	if (REQUEST_IS(connection->type, REQUEST_GETKEY) || REQUEST_IS(connection->type, REQUEST_HEADKEY)){
		DEBUG("[#%d] Responding with X-Ttl\n", fd);
		//Returns the number of chars put into the buffer
		__time_t expires = connection->target.key.entry->expires;
		int temp = snprintf(header, sizeof(header), "X-Ttl: %d\r\n", (expires == 0) ? expires : (expires - time_seconds));

		copy_to_outputbuffer(connection, header, temp);
	}
	connection->handler = http_respond_contentlength;
	return continue_processing;
}

state_action http_respond_contentlength(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	char header[32];
	DEBUG("[#%d] Responding with Content-Length\n", fd);
	//Returns the number of chars put into the buffer
	int temp = snprintf(header, sizeof(header), "Content-Length: %d\r\n", connection->target.key.entry->data_length);

	copy_to_outputbuffer(connection, header, temp);
	connection->handler = http_respond_responseend;
	return continue_processing;
}

void http_register_read(int epfd, cache_connection* connection){
	int extern stop_soon;

	connection_register_read(epfd, connection->client_sock);
	if (!stop_soon && rbuf_write_remaining(&connection->input)){
		http_read_handle(epfd, connection);
	}
}

state_action http_respond_reset_connection(int epfd, cache_connection* connection){
	http_cleanup(connection);
	connection->handler = http_handle_method;
	http_register_read(epfd, connection);
	return continue_processing;
}

state_action http_respond_responseend(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	DEBUG("[#%d] Responding with the newlines\n", fd);
	connection->output_buffer = http_templates[HTTPTEMPLATE_NEWLINE];
	connection->output_length = http_templates_length[HTTPTEMPLATE_NEWLINE];
	if (REQUEST_IS(connection->type, REQUEST_HTTPGET)){
		connection->handler = http_respond_contentbody;
	}
	else if (REQUEST_IS(connection->type, REQUEST_HTTPHEAD)){
		connection->handler = http_respond_reset_connection;
	}
	else{
		connection->handler = http_respond_writeonly;
	}
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
		http_cleanup(connection);
		connection->type = 0;
		connection->handler = http_handle_method;
		http_register_read(epfd, connection);
	}
	return continue_processing;
}

state_action http_respond_writeonly(int epfd, cache_connection* connection){
	DEBUG("[#%d] Sending static response\n", connection->client_sock);
	//Static response, after witing, read next request
	http_cleanup(connection);
	connection->handler = http_handle_method;
	http_register_read(epfd, connection);
	return continue_processing;
}

state_action http_respond_listing_separator(int epfd, cache_connection* connection){
	connection->output_buffer = "\r\n";
	connection->output_length = 2;
	connection->handler = http_respond_listing;

	return continue_processing;
}

state_action http_respond_close_connection(int epfd, cache_connection* connection){
	return close_connection;
}

state_action http_respond_listing(int epfd, cache_connection* connection){
	DEBUG("[#%d] Sending listing response\n", connection->client_sock);

	if (connection->target.table.start > kh_end(connection->target.table.table->cache_hash_set)){
		connection->target.table.start = kh_end(connection->target.table.table->cache_hash_set);
		connection->target.table.limit = 0;
	}else if ((connection->target.table.limit + connection->target.table.start) > kh_end(connection->target.table.table->cache_hash_set)){
		connection->target.table.limit = kh_end(connection->target.table.table->cache_hash_set) - (connection->target.table.start + 1);
	}

	if (connection->target.table.limit > 0){
		cache_entry* entry = NULL;
		int i = connection->target.table.limit + connection->target.table.start;
		while (!kh_exist(connection->target.table.table->cache_hash_set, i)){
			if (connection->target.table.limit == 0){
				return close_connection;
			}
			connection->target.table.limit--;
			i--;
		}
		entry = kh_val(connection->target.table.table->cache_hash_set, i);

		bool cleanup = false;
		if (connection->target.table.limit){
			connection->target.table.limit--;
		}
		else{
			cleanup = true;
		}

		if (entry != NULL){
			//Returns the number of chars put into the buffer
			connection->output_buffer = entry->key;
			connection->output_length = entry->key_length;
			connection->handler = http_respond_listing_separator;

			if (cleanup){
				connection->handler = http_respond_close_connection;
			}

			return continue_processing;
		}
	}

	return close_connection;
}

state_action http_respond_listingtotal(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	char header[32];

	if (REQUEST_IS(connection->type, REQUEST_GETTABLE)){
		DEBUG("[#%d] Responding with X-Total\n", fd);
		//Returns the number of chars put into the buffer
		int temp = snprintf(header, sizeof(header), "X-Total: %d\r\n", kh_n_buckets(connection->target.table.table->cache_hash_set));

		copy_to_outputbuffer(connection, header, temp);
	}

	connection->handler = http_respond_listing_separator;
	return continue_processing;
}

state_action http_respond_listingentries(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	char header[32];

	if (REQUEST_IS(connection->type, REQUEST_GETTABLE)){
		DEBUG("[#%d] Responding with X-Entries\n", fd);
		//Returns the number of chars put into the buffer
		int temp = snprintf(header, sizeof(header), "X-Entries: %d\r\n", kh_size(connection->target.table.table->cache_hash_set));

		copy_to_outputbuffer(connection, header, temp);
	}
	connection->handler = http_respond_listingtotal;
	return continue_processing;
}