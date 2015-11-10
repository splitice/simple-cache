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
#include <inttypes.h>
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

bool http_register_read(int epfd, cache_connection* connection){
	int extern stop_soon;

	bool res = connection_register_read(epfd, connection->client_sock);

	if (!stop_soon && rbuf_write_remaining(&connection->input)){
		http_read_handle(epfd, connection);
	}

	return res;
}

state_action http_respond_stats(int epfd, cache_connection* connection){
	char stat_buffer[4096];
	char header_buffer[1024];
	char *stat_ptr = stat_buffer;
	db_details* details = db_get_details();

	stat_ptr += snprintf(stat_ptr, sizeof(stat_buffer) - (stat_ptr - stat_buffer), "DB Keys: %" PRIu64 "\r\n", details->db_keys);
	stat_ptr += snprintf(stat_ptr, sizeof(stat_buffer) - (stat_ptr - stat_buffer), "DB Size Bytes: %" PRIu64 "\r\n", details->db_size_bytes);
	stat_ptr += snprintf(stat_ptr, sizeof(stat_buffer) - (stat_ptr - stat_buffer), "DB Stats Deletes: %" PRIu64 "\r\n", details->db_stats_deletes);
	stat_ptr += snprintf(stat_ptr, sizeof(stat_buffer) - (stat_ptr - stat_buffer), "DB Stats Gets: %" PRIu64 "\r\n", details->db_stats_gets);
	stat_ptr += snprintf(stat_ptr, sizeof(stat_buffer) - (stat_ptr - stat_buffer), "DB Stats Inserts: %" PRIu64 "\r\n", details->db_stats_inserts);

	int content_length = stat_ptr - stat_buffer;
	int header_length = snprintf(header_buffer, sizeof(header_buffer), http_templates[HTTPTEMPLATE_200CONTENT_LENGTH], content_length);

	//Send both headers and the content
	connection->output_buffer = connection->output_buffer_free = (char*)malloc(content_length + header_length);
	memcpy(connection->output_buffer_free, header_buffer, header_length);
	memcpy(connection->output_buffer_free + header_length, stat_buffer, content_length);
	connection->output_length = content_length + header_length;

	connection->handler = http_respond_writeonly;
	return continue_processing;
}

state_action http_respond_reset_connection(int epfd, cache_connection* connection){
	http_cleanup(connection);
	connection->handler = http_handle_method;
	bool res = http_register_read(epfd, connection);
	return res ? continue_processing : close_connection;
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
		ssize_t bytes_sent = sendfile(fd, connection->target.key.fd, &pos, temp);
		if (bytes_sent == 0) {
			DEBUG("[#%d] EOF Reached\r\n", fd);
			return close_connection;
		}else if (bytes_sent < 0){
			if (bytes_sent == EINTR || bytes_sent == EWOULDBLOCK){
				return continue_processing;
			}
			PWARN("Error sending bytes with sendfile. Closing connection.");
			return close_connection;
		}
		DEBUG("[#%d] Sendfile sent %d bytes from position %d\n", fd, bytes_sent, connection->target.key.position);
		connection->target.key.position += bytes_sent;
		DEBUG("[#%d] Position is now %d\n", fd, connection->target.key.position);
	}

	assert(connection->target.key.position <= connection->target.key.end_position);
	if (connection->target.key.position == connection->target.key.end_position){
		http_cleanup(connection);
		connection->handler = http_handle_method;
		bool res = http_register_read(epfd, connection);
		if (!res){
			return close_connection;
		}
	}
	return continue_processing;
}

state_action http_respond_writeonly(int epfd, cache_connection* connection){
	DEBUG("[#%d] Sending static response\n", connection->client_sock);
	//Static response, after witing, read next request
	http_cleanup(connection);
	connection->handler = http_handle_method;
	bool res = http_register_read(epfd, connection);
	return res ? continue_processing : close_connection;
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