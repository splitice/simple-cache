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
		rb_debug_read_check(rb, 1);
#endif
		rb->read_position++;
		buffer++;
	}

	//Go over anything from the start
	end = rbuf_read_remaining(rb) - end;
	for (int i = 0; i < end; i++){
		if (*buffer != '\r' && *buffer != '\n'){
			return;
		}
#ifdef DEBUG_BUILD
		rb_debug_read_check(rb, 1);
#endif
		rb->read_position++;
		buffer++;
	}
}

static state_action http_write_response_after_eol(int epfd, cache_connection* connection, int http_template){
	connection->handler = http_handle_eolwrite;
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	return needs_more;
}

static state_action http_write_response(int epfd, cache_connection* connection, int http_template){
	connection->handler = http_respond_writeonly;
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection_register_write(epfd, connection->client_sock);
	return registered_write;
}

static bool http_key_lookup(cache_connection* connection, int n, int epfd){
	//Key request
	//Copy the key from the buffer
	char* key = (char*)malloc(sizeof(char)* (n + 1));
	rbuf_copyn(&connection->input, key, n);
	*(key + n + 1) = 0;//Null terminate the key

	DEBUG("[#%d] Request key: \"%s\" (%d)\n", connection->client_sock, key, n);

	struct cache_entry* entry;
	//TODO: memcpy always?
	if (REQUEST_IS(connection->type, REQUEST_HTTPGET)){
		//db_entry_get_read will free key if necessary
		entry = db_entry_get_read(connection->target.table.table, key, n);
	}
	else if (REQUEST_IS(connection->type, REQUEST_HTTPPUT)){
		//It is the responsibility of db_entry_get_write to free key if necessary
		entry = db_entry_get_write(connection->target.table.table, key, n);
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
	connection->handler = http_handle_httpversion;

	if (entry){
		db_target_setup(&connection->target.key, entry, REQUEST_IS(connection->type, REQUEST_HTTPPUT));
	}
	else{
		connection->target.key.entry = NULL;
	}

	return true;
}

static inline state_action http_read_requeststartmethod(int epfd, cache_connection* connection, char* buffer, int n){
	//Check if this is never going to be valid, too long
	if (n > LONGEST_REQMETHOD){
		RBUF_READMOVE(connection->input, n + 1);
		return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
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
			return needs_more;
		}
		else if (n == 3 && rbuf_cmpn(&connection->input, "PUT", 3) == 0){
			//This is a PUT request
			connection->type = REQUEST_HTTPPUT;
			RBUF_READMOVE(connection->input, n + 1);
			return needs_more;
		}
		else if (n == 6 && rbuf_cmpn(&connection->input, "DELETE", 6) == 0){
			//This is a DELETE request
			connection->type = REQUEST_HTTPDELETE;
			RBUF_READMOVE(connection->input, n + 1);
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


	if (n != 0 && *buffer == '/'){
		//Key command
		//URL: table/key

		RBUF_READMOVE(connection->input, 1);
		char* key = (char*)malloc(sizeof(char)* (n));
		rbuf_copyn(&connection->input, key, n - 1);
		*(key + n) = 0;//Null terminate the key

		DEBUG("[#%d] Request table: \"%s\" (%d)\n", connection->client_sock, key, n);

		if (REQUEST_IS(connection->type, REQUEST_HTTPGET) || REQUEST_IS(connection->type, REQUEST_HTTPDELETE)){
			connection->target.table.table = db_table_get_read(key, n - 1);
		}
		else{
			connection->target.table.table = db_table_get_write(key, n - 1);
		}

		if (connection->target.table.table){
			connection->state++;
		}
		else{
			connection->state = 0;
			return http_write_response_after_eol(epfd, connection, HTTPTEMPLATE_FULL404);
		}

		RBUF_READMOVE(connection->input, n);
		return needs_more;
	}
	else if (*buffer == ' '){
		//Table command
		//URL: table
		connection->state = 0;

		if (REQUEST_IS(connection->type, REQUEST_HTTPGET)){

		}
		else if (REQUEST_IS(connection->type, REQUEST_HTTPDELETE)){

		}
		else
		{
			return http_write_response_after_eol(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
		}
	}

	return continue_processing;
}

static state_action http_read_requeststarturl2(int epfd, cache_connection* connection, char* buffer, int n)
{
	if (*buffer == ' '){
		int temporary = http_key_lookup(connection, n, epfd);
		RBUF_READMOVE(connection->input, n + 1);
		connection->state = 0;

		return temporary ? needs_more : registered_write;
	}

	return continue_processing;
}

static state_action http_read_headers(int epfd, cache_connection* connection, char* buffer, int n, uint8_t& temporary){
	if (*buffer == ':'){
		if (REQUEST_IS(connection->type, REQUEST_HTTPPUT)){
			int bytes = buffer - RBUF_READ(connection->input);
			DEBUG("[#%d] Found header of length %d\n", connection->client_sock, bytes);
			if (bytes == 14 && rbuf_cmpn(&connection->input, "Content-Length", 14) == 0){
				DEBUG("[#%d] Found Content-Length header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, n + 1);
				connection->state = HEADER_CONTENTLENGTH;
				connection->handler = http_handle_headers_extract;
				return needs_more;
			}
			if (bytes == 5 && rbuf_cmpn(&connection->input, "X-Ttl", 5) == 0){
				DEBUG("[#%d] Found X-Ttl header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, n + 1);
				connection->state = HEADER_XTTL;
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
				if (connection->target.key.entry != NULL){
					if (REQUEST_IS(connection->type, REQUEST_HTTPPUT)){
						if (connection->target.key.entry->data_length == 0){
							return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
						}
					}

					if (REQUEST_IS(connection->type, REQUEST_HTTPGET)){
						connection->handler = http_respond_start;
						connection_register_write(epfd, connection->client_sock);
						return needs_more;
					}
					else{
						connection->output_buffer = http_templates[HTTPTEMPLATE_HEADERS200];
						connection->output_length = http_templates_length[HTTPTEMPLATE_HEADERS200];
						connection->handler = http_handle_request_body;
						return needs_more;
					}
				}
				else{
					connection->output_buffer = http_templates[HTTPTEMPLATE_FULL404];
					connection->output_length = http_templates_length[HTTPTEMPLATE_FULL404];
					return needs_more;
				}
			}
		}

		//Move pointers to next record
		RBUF_READMOVE(connection->input, n + 1);
		n = -1;
	}
	else if (*buffer != '\r'){
		temporary = 0;
	}
	return continue_processing;
}

static state_action http_read_header_extraction(int epfd, cache_connection* connection, char* buffer, int n, uint8_t &temporary){
	if (*buffer == ' ' && !temporary){
		RBUF_READMOVE(connection->input, 1);
		n--;
	}
	else if (*buffer == '\n' || *buffer == '\r'){
		//We are going to have to skip another char if \r\n
		if (*buffer == '\r'){
			temporary = 2;
		}
		else{
			temporary = 1;
		}

		switch (connection->state){
		case HEADER_CONTENTLENGTH:
			int content_length;
			if (!rbuf_strntol(&connection->input, &content_length, n)){
				WARN("Invalid Content-Length value provided");

				//This is an INVALID request
				RBUF_READMOVE(connection->input, n + temporary);
				return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
			}

			//Else: We are writing, initalize fd now
			DEBUG("[#%d] Content-Length of %d found\n", connection->client_sock, content_length);
			db_target_write_allocate(&connection->target.key, content_length);

			if (IS_SINGLE_FILE(connection->target.key.entry)){
				connection->target.key.end_position = connection->target.key.entry->data_length;
			}
			else{
				connection->target.key.end_position = connection->target.key.position + connection->target.key.entry->data_length;
			}

			connection->state = 0;
			connection->handler = http_handle_headers;
			RBUF_READMOVE(connection->input, n + temporary);
			return needs_more;

		case HEADER_XTTL:
			int ttl;
			if (!rbuf_strntol(&connection->input, &ttl, n)){
				WARN("Invalid X-Ttl value provided");

				//This is an INVALID request
				RBUF_READMOVE(connection->input, n + temporary);
				return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
			}

			//Else: We are writing, initalize fd now
			DEBUG("[#%d] X-Ttl of %d found\n", connection->client_sock, ttl);

			if (ttl != 0){
				connection->target.key.entry->expires = ttl + current_time.tv_sec;
			}

			connection->state = 0;
			connection->handler = http_handle_headers;
			RBUF_READMOVE(connection->input, n + temporary);
			return needs_more;
		}
	}
	else{
		temporary = 1;
	}
}

static state_action http_read_eol(int epfd, cache_connection* connection, char* buffer, int n){
	if (*buffer == '\n'){
		connection->handler = http_respond_writeonly;
		connection_register_write(epfd, connection->client_sock);

		if (n != 0){
			RBUF_READMOVE(connection->input, n);
		}
		return registered_write;
	}
}


static state_action http_read_version(int epfd, cache_connection* connection, char* buffer, int n, uint8_t& temporary){
	if (*buffer == '\n'){
		//TODO: handle version differences
		connection->handler = http_handle_headers;

		RBUF_READMOVE(connection->input, n + 1);
		return needs_more;
	}
}

state_action http_handle_method(int epfd, cache_connection* connection){
	char* buffer;
	int end,  n;
	state_action ret = continue_processing;

	DEBUG("[#%d] Handling HTTP method\n", connection->client_sock);

	//Skip newlines at begining of request (bad clients)
	skip_over_newlines(&connection->input);

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

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_version(epfd, connection, buffer, n, connection->state));
	if (n != 0){
		RBUF_READMOVE(connection->input, n);
	}
	return ret;
}

state_action http_handle_eolwrite(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP EOL Search, then writing state\n", connection->client_sock);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_eol(epfd, connection, buffer, n));
	if (n != 0){
		RBUF_READMOVE(connection->input, n);
	}

	return needs_more;
}

state_action http_handle_headers_extract(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP Header extraction\n", connection->client_sock);
	int temporary = 0;
	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_header_extraction(epfd, connection, buffer, n, connection->state));
	return needs_more;
}

state_action http_handle_headers(int epfd, cache_connection* connection){
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP headers (initial: %d)\n", connection->client_sock, connection->state);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_headers(epfd, connection, buffer, n, connection->state));

	if (rbuf_write_remaining(&connection->input) == 0){
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
		// Write data
		lseek(connection->target.key.fd, connection->target.key.position, SEEK_SET);
		int read_bytes = write(connection->target.key.fd, RBUF_READ(connection->input), to_write);

		//Handle the bytes written
		DEBUG("[#%d] %d bytes to fd %d\n", connection->client_sock, read_bytes, connection->target.key.fd);
		RBUF_READMOVE(connection->input, read_bytes);
		connection->target.key.position += read_bytes;
	}

	//Check if done
	assert((connection->target.key.end_position - connection->target.key.position) >= 0);
	if (connection->target.key.end_position == connection->target.key.position){
		connection->output_buffer = http_templates[HTTPTEMPLATE_FULL200OK];
		connection->output_length = http_templates_length[HTTPTEMPLATE_FULL200OK];
		connection->handler = http_respond_writeonly;
		connection_register_write(epfd, connection->client_sock);

		//Decrease refs, done with writing
		connection->target.key.entry->writing = false;
		db_target_close(&connection->target.key);
	}
}