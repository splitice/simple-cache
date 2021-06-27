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
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include "http.h"
#include "config.h"
#include "debug.h"
#include "connection.h"
#include "db.h"
#include "timer.h"
#include "http_parse.h"
#include "http_respond.h"

static void copy_to_outputbuffer(scache_connection* connection, char* header, int length) {
	connection->output_buffer = connection->output_buffer_free = (char*)malloc(length);
	memcpy(connection->output_buffer_free, header, length);
	connection->output_length = length;
}

state_action http_respond_expires(scache_connection* connection) {
	int fd = connection->client_sock;
	char header[32];

	if (REQUEST_IS(connection->type, REQUEST_GETKEY) || REQUEST_IS(connection->type, REQUEST_HEADKEY)) {
		DEBUG("[#%d] Responding with X-Ttl\n", fd);
		//Returns the number of chars put into the buffer
		__time_t expires = connection->cache.target.key.entry->expires;
		int temp = snprintf(header, sizeof(header), "X-Ttl: %ld\r\n", (expires == 0) ? expires : (expires - current_time.tv_sec));

		copy_to_outputbuffer(connection, header, temp);
	}
	CONNECTION_HANDLER(connection,  http_respond_contentlength);
	return continue_processing;
}

state_action http_respond_contentlength(scache_connection* connection) {
	int fd = connection->client_sock;
	char header[32];
	DEBUG("[#%d] Responding with Content-Length\n", fd);
	//Returns the number of chars put into the buffer
	int temp = snprintf(header, sizeof(header), "Content-Length: %d\r\n", connection->cache.target.key.entry->data_length);

	copy_to_outputbuffer(connection, header, temp);
	CONNECTION_HANDLER(connection,  http_respond_responseend);
	return continue_processing;
}

bool http_register_read(scache_connection* connection) {

	bool res = connection_register_read(connection->client_sock);

	if (!connection_stop_soon() && rbuf_write_remaining(&connection->input)) {
		http_read_handle(connection);
	}

	return res;
}

state_action http_respond_stats(scache_connection* connection) {
	char stat_buffer[4096];
	char header_buffer[1024];
	char *stat_ptr = stat_buffer;
	db_details* details = db_get_details();

	stat_ptr += snprintf(stat_ptr, sizeof(stat_buffer) - (stat_ptr - stat_buffer), "DB Keys: %" PRIu64 "\r\n", details->db_keys);
	stat_ptr += snprintf(stat_ptr, sizeof(stat_buffer) - (stat_ptr - stat_buffer), "DB Tables: %u\r\n", kh_size(details->tables));
	stat_ptr += snprintf(stat_ptr, sizeof(stat_buffer) - (stat_ptr - stat_buffer), "DB Size Bytes: %" PRIu64 "\r\n", details->db_size_bytes);
	stat_ptr += snprintf(stat_ptr, sizeof(stat_buffer) - (stat_ptr - stat_buffer), "DB Blocks Free: %" PRIu32 "/%" PRIu32 "\r\n", details->blocks_free, details->blocks_exist);
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

	CONNECTION_HANDLER(connection,  http_respond_writeonly);
	return continue_processing;
}

state_action http_respond_reset_connection(scache_connection* connection) {
	http_cleanup(connection);
	CONNECTION_HANDLER(connection,  http_cache_handle_method);
	bool res = http_register_read(connection);
	return res ? continue_processing : close_connection;
}

state_action http_respond_responseend(scache_connection* connection) {
	int fd = connection->client_sock;
	DEBUG("[#%d] Responding with the newlines\n", fd);
	connection->output_buffer = http_templates[HTTPTEMPLATE_NEWLINE];
	connection->output_length = http_templates_length[HTTPTEMPLATE_NEWLINE];
	if (REQUEST_IS(connection->type, REQUEST_HTTPGET)) {
		CONNECTION_HANDLER(connection,  http_respond_contentbody);
	}
	else if (REQUEST_IS(connection->type, REQUEST_HTTPHEAD)) {
		CONNECTION_HANDLER(connection,  http_respond_reset_connection);
	}
	else{
		CONNECTION_HANDLER(connection,  http_respond_writeonly);
	}
	return continue_processing;
}

state_action http_respond_contentbody(scache_connection* connection) {
	int fd = connection->client_sock;
	DEBUG("[#%d] Sending response body\n", fd);
	//The number of bytes to read
	size_t temp = connection->cache.target.key.end_position - connection->cache.target.key.position;
	DEBUG("[#%d] To send %d bytes to the socket (len: %d, pos: %u)\n", fd, temp, connection->cache.target.key.entry->data_length, connection->cache.target.key.position);
	assert(temp >= 0);
	if (temp != 0) {
		off_t pos = connection->cache.target.key.position;
		ssize_t bytes_sent = sendfile64(fd, connection->cache.target.key.fd, &pos, temp);
		if (bytes_sent == 0) {
			DEBUG("[#%d] EOF Reached\r\n", fd);
			return close_connection;
		}else if (bytes_sent == -1) {
			if (errno == EINTR || errno == EWOULDBLOCK) {
				return continue_processing;
			}
			PWARN("Error sending bytes with sendfile. Closing connection.");
			return close_connection;
		}
		DEBUG("[#%d] Sendfile sent %d bytes from position %d\n", fd, bytes_sent, connection->cache.target.key.position);
		connection->cache.target.key.position += bytes_sent;
		DEBUG("[#%d] Position is now %d\n", fd, connection->cache.target.key.position);
	}

	assert(connection->cache.target.key.position <= connection->cache.target.key.end_position);
	if (connection->cache.target.key.position == connection->cache.target.key.end_position) {
		http_cleanup(connection);
		CONNECTION_HANDLER(connection,  http_cache_handle_method);
		bool res = http_register_read(connection);
		if (!res) {
			return close_connection;
		}
	}
	return continue_processing;
}

state_action http_respond_writeonly(scache_connection* connection) {
	DEBUG("[#%d] Sending static response then closing\n", connection->client_sock);
	//Static response, after witing, read next request
	http_cleanup(connection);
	if(connection->ltype == cache_listener){
		CONNECTION_HANDLER(connection,  http_cache_handle_method);
	} else if(connection->ltype == mon_listener) {
		CONNECTION_HANDLER(connection,  http_mon_handle_method);
	} else{
		assert(connection->ltype == cache_listener || connection->ltype == mon_listener);
	}
	bool res = http_register_read(connection);
	return res ? continue_processing : close_connection;
}

state_action http_respond_cleanupafterwrite(scache_connection* connection) {
	DEBUG("[#%d] Writing complete\n", connection->client_sock);

	connection->output_buffer = NULL;
	connection->output_length = 0;

	CONNECTION_HANDLER(connection, http_discard_input);
	connection_register_read(connection->client_sock);

	return continue_processing;
}

state_action http_respond_listing_separator(scache_connection* connection) {
	connection->output_buffer = "\r\n";
	connection->output_length = 2;
	CONNECTION_HANDLER(connection,  http_respond_listing);

	return continue_processing;
}

state_action http_respond_close_connection(scache_connection* connection) {
	return close_connection;
}

state_action http_respond_listing(scache_connection* connection) {
	DEBUG("[#%d] Sending listing response\n", connection->client_sock);

	if (connection->cache.target.table.start > kh_end(connection->cache.target.table.table->cache_hash_set)) {
		connection->cache.target.table.start = kh_end(connection->cache.target.table.table->cache_hash_set);
		connection->cache.target.table.limit = 0;
	}else if ((connection->cache.target.table.limit + connection->cache.target.table.start) > kh_end(connection->cache.target.table.table->cache_hash_set)) {
		connection->cache.target.table.limit = kh_end(connection->cache.target.table.table->cache_hash_set) - (connection->cache.target.table.start + 1);
	}

	if (connection->cache.target.table.limit > 0) {
		cache_entry* entry = NULL;
		int i = connection->cache.target.table.limit + connection->cache.target.table.start;
		while (!kh_exist(connection->cache.target.table.table->cache_hash_set, i)) {
			if (connection->cache.target.table.limit == 0) {
				return close_connection;
			}
			connection->cache.target.table.limit--;
			i--;
		}
		entry = kh_val(connection->cache.target.table.table->cache_hash_set, i);

		bool cleanup = false;
		if (connection->cache.target.table.limit) {
			connection->cache.target.table.limit--;
		}
		else{
			cleanup = true;
		}

		if (entry != NULL) {
			//Returns the number of chars put into the buffer
			connection->output_buffer = entry->key;
			connection->output_length = entry->key_length;
			CONNECTION_HANDLER(connection,  http_respond_listing_separator);

			if (cleanup) {
				CONNECTION_HANDLER(connection,  http_respond_close_connection);
			}

			return continue_processing;
		}
	}

	return close_connection;
}

state_action http_respond_listingtotal(scache_connection* connection) {
	int fd = connection->client_sock;
	char header[32];

	if (REQUEST_IS(connection->type, REQUEST_GETTABLE)) {
		DEBUG("[#%d] Responding with X-Total\n", fd);
		//Returns the number of chars put into the buffer
		int temp = snprintf(header, sizeof(header), "X-Total: %d\r\n", kh_n_buckets(connection->cache.target.table.table->cache_hash_set));

		copy_to_outputbuffer(connection, header, temp);
	}

	CONNECTION_HANDLER(connection,  http_respond_listing_separator);
	return continue_processing;
}

state_action http_respond_listingentries(scache_connection* connection) {
	int fd = connection->client_sock;
	char header[32];

	if (REQUEST_IS(connection->type, REQUEST_GETTABLE)) {
		DEBUG("[#%d] Responding with X-Entries\n", fd);
		//Returns the number of chars put into the buffer
		int temp = snprintf(header, sizeof(header), "X-Entries: %d\r\n", kh_size(connection->cache.target.table.table->cache_hash_set));

		copy_to_outputbuffer(connection, header, temp);
	}
	CONNECTION_HANDLER(connection,  http_respond_listingtotal);
	return continue_processing;
}