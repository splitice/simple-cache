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

static void skip_over_newlines(struct read_buffer* rb){
	char* buffer;
	int end = rbuf_read_to_end(rb);

	//Nothing to skip
	if (end == 0){
		return;
	}

	//Iterate over buffer until the end of the buffer
	buffer = RBUF_READPTR(rb);
	for (int i = 0; i < end; i++){
		if (*buffer != '\r' && *buffer != '\n'){
			return;
		}
#ifdef DEBUG_BUILD
		rbuf_debug_read_check(rb, 1);
#endif
		rb->read_position++;
		rbuf_debug_check(rb);
		buffer++;
	}

	//Go over anything from the start
	end = rbuf_read_remaining(rb) - end;
	for (int i = 0; i < end; i++){
		if (*buffer != '\r' && *buffer != '\n'){
			return;
		}
#ifdef DEBUG_BUILD
		rbuf_debug_read_check(rb, 1);
#endif
		rb->read_position++;
		rbuf_debug_check(rb);
		buffer++;
	}
}

static state_action http_stats_after_eol(int epfd, cache_connection* connection){
	connection->state = 0;
	connection->handler = http_handle_eolstats;
	return needs_more;
}

static state_action http_write_response_after_eol(int epfd, cache_connection* connection, int http_template){
	connection->handler = http_handle_eolwrite;
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 1;
	return needs_more;
}

static state_action http_write_response(int epfd, cache_connection* connection, int http_template){
	connection->handler = http_respond_writeonly;
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 0;
	bool res = connection_register_write(epfd, connection->client_sock);
	return res?registered_write:close_connection;
}

static state_action http_headers_response_after_eol(int epfd, cache_connection* connection, int http_template){
	connection->handler = http_handle_eolwrite;
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 2;
	return needs_more;
}

static bool http_key_lookup(cache_connection* connection, int n, int epfd){
	//Key request
	//Copy the key from the buffer
	char* key = (char*)malloc(sizeof(char)* (n + 1));
	rbuf_copyn(&connection->input, key, n);
	*(key + n) = 0;//Null terminate the key

	DEBUG("[#%d] Request key: \"%s\" (%d)\n", connection->client_sock, key, n);

	struct cache_entry* entry;
	//TODO: memcpy always?
	if (REQUEST_IS(connection->type, REQUEST_HTTPGET) || REQUEST_IS(connection->type, REQUEST_HTTPHEAD)){
		//db_entry_get_read will free key if necessary
		entry = db_entry_get_read(connection->target.table.table, key, n);
	}
	else if (REQUEST_IS(connection->type, REQUEST_HTTPPUT)){
		//It is the responsibility of db_entry_get_write to free key if necessary
		entry = db_entry_get_write(connection->target.table.table, key, n);
		if (entry){
			connection->writing = true;
		}
	}
	else if (REQUEST_IS(connection->type, REQUEST_HTTPDELETE)){
		//It is the responsibility of db_entry_get_write to free key if necessary
		entry = db_entry_get_delete(connection->target.table.table, key, n);

		connection->output_buffer = http_templates[HTTPTEMPLATE_FULLHTTP200DELETED];
		connection->output_length = http_templates_length[HTTPTEMPLATE_FULLHTTP200DELETED];
	}
	else{
		return http_write_response_after_eol(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
	}

	connection->type |= REQUEST_LEVELKEY;
	connection->type &= ~REQUEST_LEVELTABLE;
	connection->handler = http_handle_httpversion;

	if (entry){
		connection->target.key.entry = entry;
		db_target_setup(&connection->target.key, entry, REQUEST_IS(connection->type, REQUEST_HTTPPUT));
	}
	else{
		db_table_close(connection->target.table.table);
		connection->target.key.entry = NULL;
		connection->target.key.fd = -1;
	}

	return true;
}

static inline state_action http_read_requeststartmethod(int epfd, cache_connection* connection, char* buffer, int n){
	//Check if this is never going to be valid, too long
	if (n > LONGEST_REQMETHOD){
		RBUF_READMOVE(connection->input, n + 1);
		return http_write_response_after_eol(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
	}

	//A space signifies the end of the method
	if (*buffer == ' '){
		DEBUG("[#%d] Found first space seperator, len: %d\n", connection->client_sock, n);

		//As long as the method is valid the next step
		//is to parse the url
		connection->handler = http_handle_url;

		//Workout what valid method we have been given (if any)
		if (n == 3 && rbuf_cmpn(&connection->input, "GET", 3) == 0){
			//This is a GET request
			connection->type = REQUEST_HTTPGET;
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP GET Request\n", connection->client_sock);
			return needs_more;
		}
		else if (n == 3 && rbuf_cmpn(&connection->input, "PUT", 3) == 0){
			//This is a PUT request
			connection->type = REQUEST_HTTPPUT;
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP PUT Request\n", connection->client_sock);
			return needs_more;
		}
		else if (n == 4 && rbuf_cmpn(&connection->input, "HEAD", 4) == 0){
			//This is a HEAD request
			connection->type = REQUEST_HTTPHEAD;
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP HEAD Request\n", connection->client_sock);
			return needs_more;
		}
		else if (n == 6 && rbuf_cmpn(&connection->input, "DELETE", 6) == 0){
			DEBUG("[#%d] HTTP DELETE Request\n", connection->client_sock);
			//This is a DELETE request
			connection->type = REQUEST_HTTPDELETE;
			RBUF_READMOVE(connection->input, n + 1);
			return needs_more;
		}
		else if (n == 4 && rbuf_cmpn(&connection->input, "BULK", 4) == 0){
			//This is a BULK request
			connection->type = REQUEST_HTTPBULK;
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP BULK Request\n", connection->client_sock);
			return needs_more;
		}

		//Else: This is an INVALID request
		RBUF_READMOVE(connection->input, n + 1);
		return http_write_response_after_eol(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
	}

	return continue_processing;
}

static inline state_action http_read_requeststarturl1(int epfd, cache_connection* connection, char* buffer, int n){
	//Assert: first char is a / (start of URL)
	assert(n != 0 || *buffer == '/');


	if (n != 0){
		//Skip unless character has meaning
		if (*buffer != '/' && *buffer != ' ') {
			return continue_processing;//continue
		}


		RBUF_READMOVE(connection->input, 1);
		char* key = (char*)malloc(sizeof(char)* (n));
		rbuf_copyn(&connection->input, key, n - 1);
		*(key + (n - 1)) = 0;//Null terminate the key
		DEBUG("[#%d] Request table: \"%s\" (%d)\n", connection->client_sock, key, n);
		connection->type |= REQUEST_LEVELTABLE;

		if (*buffer == '/'){
			//Key command
			//URL: table/key


			if (REQUEST_IS(connection->type, REQUEST_HTTPGET) || REQUEST_IS(connection->type, REQUEST_HTTPDELETE) || REQUEST_IS(connection->type, REQUEST_HTTPHEAD)){
				connection->target.table.table = db_table_get_read(key, n - 1);
			}
			else{
				connection->target.table.table = db_table_get_write(key, n - 1);
			}

			if (connection->target.table.table){
				connection->state++;
			}
			else{
				return http_headers_response_after_eol(epfd, connection, HTTPTEMPLATE_FULL404);
			}

			RBUF_READMOVE(connection->input, n);
			return needs_more;
		}
		else if (*buffer == ' '){
			//Table command
			//URL: table
			connection->target.table.table = db_table_get_read(key, n - 1);
			connection->target.table.start = 0;
			connection->target.table.limit = DEFAULT_LISTING_LIMIT;

			RBUF_READMOVE(connection->input, n + 1);
			if (REQUEST_IS(connection->type, REQUEST_HTTPGET) || REQUEST_IS(connection->type, REQUEST_HTTPDELETE) || REQUEST_IS(connection->type, REQUEST_HTTPBULK)){
				//Request for "/"
				if (n == 1){
					//Stats Command
					return http_stats_after_eol(epfd, connection);
				}

				//Else request for table/key
				connection->handler = http_handle_headers;
				connection->state = 0;
				return needs_more;
			}
			else
			{
				return http_write_response_after_eol(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
			}
		}
	}

	return continue_processing;
}

static state_action http_read_requeststarturl2(int epfd, cache_connection* connection, char* buffer, int n)
{
	if (*buffer == ' '){
		bool temporary = http_key_lookup(connection, n, epfd);
		RBUF_READMOVE(connection->input, n + 1);
		connection->state = 0;

		return temporary ? needs_more : registered_write;
	}

	return continue_processing;
}

static state_action http_read_headers(int epfd, cache_connection* connection, char* buffer, int n, uint32_t& temporary){
	if (*buffer == ':'){
		int bytes = buffer - RBUF_READ(connection->input);
		if (bytes < 0){
			bytes = rbuf_read_to_end(&connection->input) + (buffer - RBUF_START(connection->input));
		}
		DEBUG("[#%d] Found header of length %d\n", connection->client_sock, bytes);

		if (REQUEST_IS(connection->type, REQUEST_HTTPPUT | REQUEST_LEVELKEY)){
			if (bytes == 14 && rbuf_cmpn(&connection->input, "Content-Length", 14) == 0){
				DEBUG("[#%d] Found Content-Length header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_CONTENTLENGTH;
				connection->handler = http_handle_headers_extract;
				return needs_more;
			}
			if (bytes == 5 && rbuf_cmpn(&connection->input, "X-Ttl", 5) == 0){
				DEBUG("[#%d] Found X-Ttl header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_XTTL;
				connection->handler = http_handle_headers_extract;
				return needs_more;
			}
		}
		else if (REQUEST_IS(connection->type, REQUEST_HTTPGET | REQUEST_LEVELTABLE)){
			if (bytes == 7 && rbuf_cmpn(&connection->input, "X-Start", 7) == 0){
				DEBUG("[#%d] Found X-Start header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_XSTART;
				connection->handler = http_handle_headers_extract;
				return needs_more;
			}
			if (bytes == 7 && rbuf_cmpn(&connection->input, "X-Limit", 7) == 0){
				DEBUG("[#%d] Found X-Limit header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_XLIMIT;
				connection->handler = http_handle_headers_extract;
				return needs_more;
			}
		}
		else if (REQUEST_IS(connection->type, REQUEST_HTTPBULK | REQUEST_LEVELTABLE)){
			if (bytes == 8 && rbuf_cmpn(&connection->input, "X-Delete", 8) == 0){
				DEBUG("[#%d] Found X-Delete header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_XDELETE;
				connection->handler = http_handle_headers_extract;
				return needs_more;
			}
		}
	}
	else if (*buffer == '\n'){
		temporary++;
		if (temporary == 2){
			RBUF_READMOVE(connection->input, n + 1);

			if (REQUEST_IS(connection->type, REQUEST_LEVELKEY)) {
				if (REQUEST_IS(connection->type, REQUEST_HTTPPUT)){
					if (connection->target.key.entry != NULL && connection->target.key.entry->data_length == 0){
						DEBUG("[#%d] No valid content-length provided for PUT request\n", connection->client_sock);
						return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDCONTENTLENGTH);
					}

					connection->handler = http_handle_request_body;
					return needs_more;
				}
				else if (connection->target.key.entry != NULL){
					if (REQUEST_IS(connection->type, REQUEST_HTTPGET) || REQUEST_IS(connection->type, REQUEST_HTTPHEAD)){
						connection->output_buffer = http_templates[HTTPTEMPLATE_HEADERS200];
						connection->output_length = http_templates_length[HTTPTEMPLATE_HEADERS200];
						connection->state = 0;

						connection->handler = http_respond_start;
						bool res = connection_register_write(epfd, connection->client_sock);
						return res ? registered_write : close_connection;
					}
					else if (REQUEST_IS(connection->type, REQUEST_HTTPDELETE)){
						db_entry_handle_delete(connection->target.key.entry);

						return http_write_response(epfd, connection, HTTPTEMPLATE_FULLHTTP200DELETED);
					}
					else{
						FATAL("Unknown request type");
					}
				}
				else{
					return http_write_response(epfd, connection, HTTPTEMPLATE_FULL404);
				}
			}
			else{
				//Table level
				if (REQUEST_IS(connection->type, REQUEST_HTTPGET)){
					if (connection->target.table.table){
						connection->output_buffer = http_templates[HTTPTEMPLATE_HEADERS200_CONCLOSE];
						connection->output_length = http_templates_length[HTTPTEMPLATE_HEADERS200_CONCLOSE];
						connection->handler = http_respond_listingentries;
						bool res = connection_register_write(epfd, connection->client_sock);
						return res ? registered_write : close_connection;
					}
					else
					{
						return http_write_response(epfd, connection, HTTPTEMPLATE_FULL404);
					}
					
				}
				else if (REQUEST_IS(connection->type, REQUEST_HTTPDELETE)){

					if (connection->target.table.table){
						db_table_handle_delete(connection->target.table.table);
						return http_write_response(epfd, connection, HTTPTEMPLATE_FULLHTTP200DELETED);
					}
					else
					{
						return http_write_response(epfd, connection, HTTPTEMPLATE_FULL404);
					}
				}
				else if (REQUEST_IS(connection->type, REQUEST_HTTPBULK)){
					return http_write_response(epfd, connection, HTTPTEMPLATE_BULK_OK);
				}
			}
		}

		//Move pointers to next record
		RBUF_READMOVE(connection->input, n + 1);
		return needs_more;
		//n = -1;
		//return 
	}
	else if (*buffer != '\r'){
		temporary = 0;
	}
	return continue_processing;
}

static state_action http_read_header_extraction(int epfd, cache_connection* connection, char* buffer, int n){
	int temporary = 0;
	if (*buffer == ' ' && !temporary){
		RBUF_READMOVE(connection->input, 1);
	}
	else if (*buffer == '\n' || *buffer == '\r'){
		//We are going to have to skip another char if \r\n
		if (*buffer == '\r'){
			temporary = 2;
		}
		else{
			temporary = 1;
		}

		int length = buffer - RBUF_READ(connection->input);

		switch (connection->state){
		case HEADER_CONTENTLENGTH:
			if (REQUEST_IS(connection->type, REQUEST_HTTPPUT | REQUEST_LEVELKEY)){
				int content_length;
				if (!rbuf_strntol(&connection->input, &content_length, length)){
					WARN("Invalid Content-Length value provided");

					//This is an INVALID request
					RBUF_READMOVE(connection->input, length + temporary);
					return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
				}

				//Else: We are writing, initalize fd now
				DEBUG("[#%d] Content-Length of %d found\n", connection->client_sock, content_length);

				if (connection->target.key.entry != NULL){
					db_target_write_allocate(&connection->target.key, content_length);

					if (IS_SINGLE_FILE(connection->target.key.entry)){
						connection->target.key.end_position = connection->target.key.entry->data_length;
					}
					else{
						connection->target.key.end_position = connection->target.key.position + connection->target.key.entry->data_length;
					}
				}
				else{
					connection->target.key.position = 0;
					connection->target.key.end_position = content_length;
				}
			}
			break;

		case HEADER_XTTL:
			if (REQUEST_IS(connection->type, REQUEST_HTTPPUT | REQUEST_LEVELKEY)){
				if (connection->target.key.entry != NULL){
					int ttl;
					if (!rbuf_strntol(&connection->input, &ttl, length)){
						WARN("Invalid X-Ttl value provided");

						//This is an INVALID request
						RBUF_READMOVE(connection->input, length + temporary);
						return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
					}

					//Else: We are writing, initalize fd now
					DEBUG("[#%d] X-Ttl of %d found\n", connection->client_sock, ttl);

					if (ttl != 0){
						connection->target.key.entry->expires = ttl + time_seconds;
					}
				}
			}

			break;

		case HEADER_XLIMIT:
			if (REQUEST_IS(connection->type, REQUEST_HTTPGET | REQUEST_LEVELTABLE)){
				int limit;
				if (!rbuf_strntol(&connection->input, &limit, length)){
					WARN("Invalid X-Limit value provided");

					//This is an INVALID request
					RBUF_READMOVE(connection->input, length + temporary);
					return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
				}

				//Else: We are writing, initalize fd now
				DEBUG("[#%d] X-Limit of %d found\n", connection->client_sock, limit);

				if (limit >= 0){
					connection->target.table.limit = limit;
				}
			}
			break;

		case HEADER_XSTART:
			if (REQUEST_IS(connection->type, REQUEST_HTTPGET | REQUEST_LEVELTABLE)){
				int start;
				if (!rbuf_strntol(&connection->input, &start, length)){
					WARN("Invalid X-Start value provided");

					//This is an INVALID request
					RBUF_READMOVE(connection->input, length + temporary);
					return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
				}

				//Else: We are writing, initalize fd now
				DEBUG("[#%d] X-Start of %d found\n", connection->client_sock, start);

				if (start >= 0){
					connection->target.table.start = start;
				}
			}
			break;

		case HEADER_XDELETE:
			if (REQUEST_IS(connection->type, REQUEST_HTTPBULK | REQUEST_LEVELTABLE)){
				char* key = (char*)malloc(length);
				rbuf_copyn(&connection->input, key, length);
				cache_entry* entry = db_entry_get_delete(connection->target.table.table, key, length);
				if (entry != NULL){
					db_entry_handle_delete(entry);
					db_entry_deref(entry, false);
				}
			}
			break;
		}


		connection->state = 1;
		connection->handler = http_handle_headers;
		RBUF_READMOVE(connection->input, length + temporary);
		return needs_more;
	}
	else{
		temporary = 1;
	}
	return continue_processing;
}

static state_action http_read_eol(int epfd, cache_connection* connection, char* buffer, int n, uint32_t& temporary){
	uint16_t target = temporary & 0x0000FFFF;
	uint16_t current = (temporary >> 16);
	if (*buffer == '\n')
	{
		current++;
		temporary = target | (current << 16);
	}
	else if (*buffer != '\r' && current != 0)
	{
		current = 0;
		temporary = target | (current << 16);
	}
	if (target == current){
		connection->state = 0;
		connection->handler = http_respond_writeonly;
		bool res = connection_register_write(epfd, connection->client_sock);

		if (!res){
			return close_connection;
		}

		if (n != 0){
			RBUF_READMOVE(connection->input, n);
		}
		return registered_write;
	}
	return continue_processing;
}


static state_action http_read_version(int epfd, cache_connection* connection, char* buffer, int& n){
	if (*buffer == '\n'){
		//TODO: handle version differences
		connection->state = 1;
		connection->handler = http_handle_headers;

		RBUF_READMOVE(connection->input, n + 1);
		n = -1;
		return needs_more;
	}
	return continue_processing;
}

state_action http_handle_method(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;

	DEBUG("[#%d] Handling HTTP method\n", connection->client_sock);

	//Skip newlines at begining of request (bad clients)
	skip_over_newlines(&connection->input);

	connection->type = 0;

	//Process request line
	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_requeststartmethod(epfd, connection, buffer, n));
	return ret;
}

state_action http_handle_url(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP url (Stage State: %d)\n", connection->client_sock, connection->state);

	if (connection->state == 0){
		RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_requeststarturl1(epfd, connection, buffer, n));
	}
	else{
		RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_requeststarturl2(epfd, connection, buffer, n));
	}
	return ret;
}

state_action http_handle_httpversion(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP version\n", connection->client_sock);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_version(epfd, connection, buffer, n));
	if (n != 0){
		RBUF_READMOVE(connection->input, n);
	}
	return ret;
}

state_action http_handle_eolstats(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP EOL Search, then writing stats\n", connection->client_sock);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_eol(epfd, connection, buffer, n, connection->state));
	if (n != 0 && ret == needs_more){
		RBUF_READMOVE(connection->input, n);
	}
	if (ret == registered_write){
		connection->handler = http_respond_stats;
	}

	return ret;
}

state_action http_handle_eolwrite(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP EOL Search, then writing state\n", connection->client_sock);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_eol(epfd, connection, buffer, n, connection->state));
	if (n != 0 && ret == needs_more){
		RBUF_READMOVE(connection->input, n);
	}

	return ret;
}

state_action http_handle_headers_extract(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP Header extraction\n", connection->client_sock);
	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_header_extraction(epfd, connection, buffer, n));
	return ret;
}

state_action http_handle_headers(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP headers (initial: %d)\n", connection->client_sock, connection->state);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_headers(epfd, connection, buffer, n, connection->state));

	//make sure we dont 100% fill up
	if (connection->handler == http_handle_headers && (&connection->input) == 0){
		return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
	}
	return ret;
}

state_action http_handle_request_body(int epfd, cache_connection* connection){
	DEBUG("[#%d] Handling STATE_REQUESTBODY\n", connection->client_sock);
	int max_write = rbuf_read_to_end(&connection->input);
	assert(max_write >= 0);
	int to_write = connection->target.key.end_position - connection->target.key.position;
	assert(to_write >= 0);

	//Limit to the ammount read from socket
	DEBUG("[#%d] Wanting to write %d bytes (max: %d) to fd %d\n", connection->client_sock, to_write, max_write, connection->target.key.fd);
	if (to_write > max_write){
		to_write = max_write;
	}

	if (to_write != 0){
		if (connection->writing){
			// Write data
			lseek(connection->target.key.fd, connection->target.key.position, SEEK_SET);
			int read_bytes = write(connection->target.key.fd, RBUF_READ(connection->input), to_write);

			//Handle the bytes written
			DEBUG("[#%d] %d bytes to fd %d\n", connection->client_sock, read_bytes, connection->target.key.fd);
			RBUF_READMOVE(connection->input, read_bytes);
			connection->target.key.position += read_bytes;
		}
		else{
			RBUF_READMOVE(connection->input, to_write);
			connection->target.key.position += to_write;
		}
	}

	//Check if done
	assert((connection->target.key.end_position - connection->target.key.position) >= 0);
	if (connection->target.key.end_position == connection->target.key.position){
		//Decrease refs, done with writing
		if (connection->writing){
			db_complete_writing(connection->target.key.entry);
			connection->writing = false;
		}

		return http_write_response(epfd, connection, HTTPTEMPLATE_FULL200OK);
	}
}