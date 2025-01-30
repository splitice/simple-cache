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

extern int epfd;

static state_action http_stats_after_eol(scache_connection* connection) {
	connection->state = 0;
	CONNECTION_HANDLER(connection,  http_cache_handle_eolstats);
	return needs_more_read;
}

static state_action http_write_response_after_eol(scache_connection* connection, int http_template) {
	CONNECTION_HANDLER(connection, http_handle_eolwritetoend);
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 1;
	return needs_more_read;
}

static state_action http_write_response(scache_connection* connection, int http_template) {
	CONNECTION_HANDLER(connection,  http_respond_writeonly);
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 0;
	bool res = connection_register_write(connection);
	return res?registered_write:close_connection;
}

static state_action http_headers_response_after_eol(scache_connection* connection, int http_template) {
	CONNECTION_HANDLER(connection,  http_handle_eolwritetoend);
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 2;
	return needs_more_read;
}

static bool http_key_lookup(scache_connection* connection, int n) {
	//Key request
	//Copy the key from the buffer
	char* key = (char*)malloc(sizeof(char)* (n + 1));
	rbuf_copyn(&connection->input, key, n);
	*(key + n) = 0;//Null terminate the key

	DEBUG("[#%d] Request key: \"%s\" (%d)\n", connection->client_sock, key, n);

	struct cache_entry* entry;
	//TODO: memcpy always?
	if (REQUEST_IS(connection->method, REQUEST_HTTPGET) || REQUEST_IS(connection->method, REQUEST_HTTPHEAD)) {
		//db_entry_get_read will free key if necessary
		entry = db_entry_get_read(connection->cache.target.table.table, key, n);
	}
	else if (REQUEST_IS(connection->method, REQUEST_HTTPPUT)) {
		//It is the responsibility of db_entry_get_write to free key if necessary
		entry = db_entry_get_write(connection->cache.target.table.table, key, n);
		if (entry) {
			connection->is_writing = true;
		}
	}
	else if (REQUEST_IS(connection->method, REQUEST_HTTPDELETE)) {
		//It is the responsibility of db_entry_get_write to free key if necessary
		entry = db_entry_get_delete(connection->cache.target.table.table, key, n);

		connection->output_buffer = http_templates[HTTPTEMPLATE_FULLHTTP200DELETED];
		connection->output_length = http_templates_length[HTTPTEMPLATE_FULLHTTP200DELETED];
	}
	else
	{
		DEBUG("[#%d] Has request key but is not GET, HEAD, PUT or DELETE (binary: %x)\n", connection->client_sock, connection->method);
		return http_write_response_after_eol(connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
	}

	int type = connection->method;
	connection->method |= REQUEST_CACHE_LEVELKEY;
	assert(REQUEST_IS(connection->method, REQUEST_CACHE_LEVELKEY));
	assert(REQUEST_IS(connection->method, connection->method));
	assert(REQUEST_IS(connection->method, type));
	connection->method &= ~REQUEST_CACHE_LEVELTABLE;
	assert(REQUEST_IS(connection->method, REQUEST_CACHE_LEVELKEY));
	assert(REQUEST_IS(connection->method, connection->method));
	assert(REQUEST_IS(connection->method, type & ~REQUEST_CACHE_LEVELTABLE));
	assert(!REQUEST_IS(connection->method, REQUEST_CACHE_LEVELTABLE));
	CONNECTION_HANDLER(connection,  http_cache_handle_httpversion);

	if (entry) {
		connection->cache.target.key.entry = entry;
		db_target_setup(&connection->cache.target.key, entry, REQUEST_IS(connection->method, REQUEST_HTTPPUT));
	}
	else{
		db_table_close(connection->cache.target.table.table);
		connection->cache.target.key.entry = NULL;
		connection->cache.target.key.fd = -1;
	}

	return true;
}

static inline state_action http_read_requeststartmethod(scache_connection* connection, char* buffer, int n) {
	//Check if this is never going to be valid, too long
	if (n > LONGEST_REQMETHOD) {
		RBUF_READMOVE(connection->input, n + 1);
		return http_write_response_after_eol(connection, HTTPTEMPLATE_FULLLONGMETHOD);
	}

	//A space signifies the end of the method
	if (*buffer == ' ') {
		DEBUG("[#%d] Found space seperator, len: %d\n", connection->client_sock, n);

		//As long as the method is valid the next step
		//is to parse the url
		CONNECTION_HANDLER(connection, http_cache_handle_url);
		connection->state = 0;

		//Workout what valid method we have been given (if any)
		if (n == 3 && rbuf_cmpn(&connection->input, "GET", 3) == 0) {
			//This is a GET request
			connection->method = REQUEST_HTTPGET;
			assert(REQUEST_IS(connection->method, connection->method));
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP GET Request\n", connection->client_sock);
			return needs_more_read;
		}
		else if (n == 3 && rbuf_cmpn(&connection->input, "PUT", 3) == 0) {
			//This is a PUT request
			connection->method = REQUEST_HTTPPUT;
			assert(REQUEST_IS(connection->method, connection->method));
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP PUT Request\n", connection->client_sock);
			return needs_more_read;
		}
		else if (n == 4 && rbuf_cmpn(&connection->input, "HEAD", 4) == 0) {
			//This is a HEAD request
			connection->method = REQUEST_HTTPHEAD;
			assert(REQUEST_IS(connection->method, connection->method));
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP HEAD Request\n", connection->client_sock);
			return needs_more_read;
		}
		else if (n == 6 && rbuf_cmpn(&connection->input, "DELETE", 6) == 0) {
			DEBUG("[#%d] HTTP DELETE Request\n", connection->client_sock);
			//This is a DELETE request
			connection->method = REQUEST_HTTPDELETE;
			assert(REQUEST_IS(connection->method, connection->method));
			RBUF_READMOVE(connection->input, n + 1);
			return needs_more_read;
		}
		else if ((n == 4 && rbuf_cmpn(&connection->input, "BULK", 4) == 0) || (n == 5 && rbuf_cmpn(&connection->input, "PURGE", 5) == 0)) {
			//This is a BULK request
			connection->method = REQUEST_HTTPPURGE;
			assert(REQUEST_IS(connection->method, connection->method));
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP BULK Request\n", connection->client_sock);
			return needs_more_read;
		}
		else if (n == 5 && rbuf_cmpn(&connection->input, "ADMIN", 5) == 0) {
			//This is a BULK request
			connection->method = REQUEST_HTTPADMIN;
			assert(REQUEST_IS(connection->method, connection->method));
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP ADMIN Request\n", connection->client_sock);
			return needs_more_read;
		}

		//Else: This is an INVALID request
		RBUF_READMOVE(connection->input, n + 1);
		return http_write_response_after_eol(connection, HTTPTEMPLATE_FULLUNKNOWNMETHOD);
	}

	return continue_processing;
}

static inline state_action http_read_requeststarturl1(scache_connection* connection, char* buffer, int n) {
	//Assert: first char is a / (start of URL)
	assert(n != 0 || *buffer == '/');

	// search either for / (goto http_read_requeststarturl2) or " " goto next step
	if (n != 0) {
		//Skip unless character has meaning
		if (*buffer != '/' && *buffer != ' ') {
			return continue_processing;//continue
		}


		RBUF_READMOVE(connection->input, 1);
		char* key = (char*)malloc(sizeof(char) * (n + 1));
		rbuf_copyn(&connection->input, key, n - 1);
		key[n - 1] = 0;//Null terminate the key
		int type = connection->method;
		connection->method |= REQUEST_CACHE_LEVELTABLE;
		assert(REQUEST_IS(connection->method, REQUEST_CACHE_LEVELTABLE));
		assert(REQUEST_IS(connection->method, connection->method));
		assert(REQUEST_IS(connection->method, type));

		if (*buffer == '/') {
			//Key command
			//URL: table/key


			if (REQUEST_IS(connection->method, REQUEST_HTTPGET) || REQUEST_IS(connection->method, REQUEST_HTTPDELETE) || REQUEST_IS(connection->method, REQUEST_HTTPHEAD)) 
			{
				connection->cache.target.table.table = db_table_get_read(key, n - 1);
			}
			else
			{
				connection->cache.target.table.table = db_table_get_write(key, n - 1);
			}

			if (connection->cache.target.table.table)
			{
				connection->state++;
			}
			else
			{
				return http_headers_response_after_eol(connection, HTTPTEMPLATE_FULL404);
			}

			RBUF_READMOVE(connection->input, n);
			return needs_more_read;
		}
		else if (*buffer == ' ') {
			if (REQUEST_IS(connection->method, REQUEST_HTTPGET) || REQUEST_IS(connection->method, REQUEST_HTTPDELETE) || REQUEST_IS(connection->method, REQUEST_HTTPPURGE)) {
				//Request for "/"
				if (n == 1) {
					//Stats Command
					DEBUG("[#%d] Request stats\n", connection->client_sock);
					free(key);
					RBUF_READMOVE(connection->input, n + 1);
					return http_stats_after_eol(connection);
				}

				//Table command
				//URL: table
				DEBUG("[#%d] Request table: \"%s\" (%d)\n", connection->client_sock, key, n);
				connection->cache.target.table.table = db_table_get_read(key, n - 1);
				connection->cache.target.table.start = 0;
				connection->cache.target.table.limit = DEFAULT_LISTING_LIMIT;
				RBUF_READMOVE(connection->input, n + 1);

				//Else request for table/key
				CONNECTION_HANDLER(connection, http_cache_handle_headers);
				connection->state = 0;
				return needs_more_read;
			} 
			
			if(REQUEST_IS(connection->method, REQUEST_HTTPADMIN)){
				DEBUG("[#%d] Request admin url: \"%s\" (%d)\n", connection->client_sock, key, n);
				if(n == 3 && strncmp(key, "gc", n	) == 0){
					free(key);
					RBUF_READMOVE(connection->input, n + 1);
					
					//GC
					db_lru_gc();
					return http_stats_after_eol(connection);
				}
			}
			
			free(key);
			RBUF_READMOVE(connection->input, n + 1);
			return http_write_response_after_eol(connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
		}
	}

	return continue_processing;
}

static state_action http_read_requeststarturl2(scache_connection* connection, char* buffer, int n)
{
	if (*buffer == ' ') {
		bool temporary = http_key_lookup(connection, n);
		RBUF_READMOVE(connection->input, n + 1);
		connection->state = 0;

		return temporary ? needs_more_read : registered_write;
	}

	return continue_processing;
}

static state_action http_read_headers(scache_connection* connection, char* buffer, int n, uint32_t& temporary) {
	if (*buffer == ':') {
		int bytes = buffer - RBUF_READ(connection->input);
		if (bytes < 0) {
			bytes = rbuf_read_to_end(&connection->input) + (buffer - RBUF_START(connection->input));
		}
		DEBUG("[#%d] Found header of length %d\n", connection->client_sock, bytes);

		if (REQUEST_IS(connection->method, REQUEST_HTTPPUT | REQUEST_CACHE_LEVELKEY)) {
			if (bytes == 14 && rbuf_cmpn(&connection->input, "Content-Length", 14) == 0) {
				DEBUG("[#%d] Found Content-Length header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_CONTENTLENGTH;
				CONNECTION_HANDLER(connection,  http_cache_handle_headers_extract);
				return needs_more_read;
			}
			if (bytes == 5 && (rbuf_cmpn(&connection->input, "X-Ttl", 5) == 0 || rbuf_cmpn(&connection->input, "x-ttl", 5) == 0)) {
				DEBUG("[#%d] Found X-Ttl header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_XTTL;
				CONNECTION_HANDLER(connection,  http_cache_handle_headers_extract);
				return needs_more_read;
			}
		}
		else if (REQUEST_IS(connection->method, REQUEST_HTTPGET | REQUEST_CACHE_LEVELTABLE)) {
			if (bytes == 7 && (rbuf_cmpn(&connection->input, "X-Start", 7) == 0 || rbuf_cmpn(&connection->input, "x-start", 7) == 0)) {
				DEBUG("[#%d] Found X-Start header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_XSTART;
				CONNECTION_HANDLER(connection,  http_cache_handle_headers_extract);
				return needs_more_read;
			}
			if (bytes == 7 && (rbuf_cmpn(&connection->input, "X-Limit", 7) == 0 || rbuf_cmpn(&connection->input, "x-limit", 7) == 0)) {
				DEBUG("[#%d] Found X-Limit header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_XLIMIT;
				CONNECTION_HANDLER(connection,  http_cache_handle_headers_extract);
				return needs_more_read;
			}
		}
		else if (REQUEST_IS(connection->method, REQUEST_HTTPPURGE | REQUEST_CACHE_LEVELTABLE)) {
			if (bytes == 8 && (rbuf_cmpn(&connection->input, "X-Delete", 8) == 0 || rbuf_cmpn(&connection->input, "x-delete", 8) == 0)) {
				DEBUG("[#%d] Found X-Delete header\n", connection->client_sock);
				RBUF_READMOVE(connection->input, bytes + 1);
				connection->state = HEADER_XDELETE;
				CONNECTION_HANDLER(connection,  http_cache_handle_headers_extract);
				return needs_more_read;
			}
		}
	}
	else if (*buffer == '\n') {
		RBUF_READMOVE(connection->input, n + 1);
		if (++temporary == 2) {
			DEBUG("[#%d] Completed request read with type %x for %d bytes\n", connection->client_sock, connection->method, n);

			// Router: Entry / Key level
			if (REQUEST_IS(connection->method, REQUEST_CACHE_LEVELKEY)) {
				if (REQUEST_IS(connection->method, REQUEST_HTTPPUT)) {
					if (connection->cache.target.key.entry != NULL && connection->cache.target.key.entry->data_length == 0) {
						DEBUG("[#%d] No valid content-length provided for PUT request\n", connection->client_sock);
						return http_write_response(connection, HTTPTEMPLATE_FULLINVALIDCONTENTLENGTH);
					}

					CONNECTION_HANDLER(connection,  http_cache_handle_request_body);
					return needs_more_read;
				}
				if (connection->cache.target.key.entry != NULL) {
					if (REQUEST_IS(connection->method, REQUEST_HTTPGET) || REQUEST_IS(connection->method, REQUEST_HTTPHEAD)) {
						connection->output_buffer = http_templates[HTTPTEMPLATE_HEADERS200];
						connection->output_length = http_templates_length[HTTPTEMPLATE_HEADERS200];
						connection->state = 0;

						CONNECTION_HANDLER(connection,  http_respond_start);
						bool res = connection_register_write(connection);
						return res ? registered_write : close_connection;
					}
					if (REQUEST_IS(connection->method, REQUEST_HTTPDELETE)) {
						db_entry_handle_delete(connection->cache.target.key.entry);

						return http_write_response(connection, HTTPTEMPLATE_FULLHTTP200DELETED);
					}
				}
				else
				{
					return http_write_response(connection, HTTPTEMPLATE_FULL404);
				}
			}
			
			//Router: Table level
			if (REQUEST_IS(connection->method, REQUEST_HTTPGET)) {
				assert(connection->input.read_position == connection->input.write_position);

				if (!connection->cache.target.table.table) {
					return http_write_response(connection, HTTPTEMPLATE_FULL404);
				}
				connection->output_buffer = http_templates[HTTPTEMPLATE_HEADERS200_CONCLOSE];
				connection->output_length = http_templates_length[HTTPTEMPLATE_HEADERS200_CONCLOSE];
				CONNECTION_HANDLER(connection,  http_respond_listingentries);
				bool res = connection_register_write(connection);
				return res ? registered_write : close_connection;
				
			}
			if (REQUEST_IS(connection->method, REQUEST_HTTPDELETE)) {
				if (!connection->cache.target.table.table) {
					return http_write_response(connection, HTTPTEMPLATE_FULL404);
				}
				db_table_handle_delete(connection->cache.target.table.table);
				return http_write_response(connection, HTTPTEMPLATE_FULLHTTP200DELETED);
			}
			if (REQUEST_IS(connection->method, REQUEST_HTTPPURGE)) {
				return http_write_response(connection, HTTPTEMPLATE_BULK_OK);
			}
			if (REQUEST_IS(connection->method, REQUEST_HTTPADMIN)) {
				return http_write_response(connection, HTTPTEMPLATE_BULK_OK);
			}

			return http_write_response(connection, HTTPTEMPLATE_FULLUNKNOWNREQUEST);
		}

		// Needs more data
		return needs_more_read;
	}
	else if (*buffer != '\r') {
		temporary = 0;
	}
	return continue_processing;
}

static state_action http_read_header_extraction(scache_connection* connection, char* buffer, int n) {
	int temporary = 0;
	if (*buffer == ' ' && !temporary) {
		RBUF_READMOVE(connection->input, 1);
	}
	else if (*buffer == '\n' || *buffer == '\r') {
		//We are going to have to skip another char if \r\n
		if (*buffer == '\r') {
			temporary = 2;
		}
		else{
			temporary = 1;
		}

		int length = buffer - RBUF_READ(connection->input);

		switch (connection->state) {
		case HEADER_CONTENTLENGTH:
			if (REQUEST_IS(connection->method, REQUEST_HTTPPUT | REQUEST_CACHE_LEVELKEY)) {
				int content_length;
				if (!rbuf_strntol(&connection->input, &content_length, length)) {
					WARN("Invalid Content-Length value provided");

					//This is an INVALID request
					RBUF_READMOVE(connection->input, length + temporary);
					return http_write_response(connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
				}

				//Else: We are writing, initalize fd now
				DEBUG("[#%d] Content-Length of %d found\n", connection->client_sock, content_length);

				if (connection->cache.target.key.entry != NULL) {
					db_target_write_allocate(&connection->cache.target.key, content_length);

					if (IS_SINGLE_FILE(connection->cache.target.key.entry)) {
						connection->cache.target.key.end_position = connection->cache.target.key.entry->data_length;
						assert(connection->cache.target.key.end_position - connection->cache.target.key.position == content_length);
					}
					else{
						connection->cache.target.key.end_position = connection->cache.target.key.position + connection->cache.target.key.entry->data_length;
						assert(connection->cache.target.key.end_position - connection->cache.target.key.position == content_length);
					}
				}
				else{
					connection->cache.target.key.position = 0;
					connection->cache.target.key.end_position = content_length;
				}
			}
			break;

		case HEADER_XTTL:
			if (REQUEST_IS(connection->method, REQUEST_HTTPPUT | REQUEST_CACHE_LEVELKEY)) {
				if (connection->cache.target.key.entry != NULL) {
					int ttl;
					if (!rbuf_strntol(&connection->input, &ttl, length)) {
						WARN("Invalid X-Ttl value provided");

						//This is an INVALID request
						RBUF_READMOVE(connection->input, length + temporary);
						return http_write_response(connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
					}

					//Else: We are writing, initalize fd now
					DEBUG("[#%d] X-Ttl of %d found\n", connection->client_sock, ttl);

					if (ttl != 0) {
						connection->cache.target.key.entry->expires = ttl + current_time.tv_sec;
					}
				}
			}

			break;

		case HEADER_XLIMIT:
			if (REQUEST_IS(connection->method, REQUEST_HTTPGET | REQUEST_CACHE_LEVELTABLE)) {
				int limit;
				if (!rbuf_strntol(&connection->input, &limit, length)) {
					WARN("Invalid X-Limit value provided");

					//This is an INVALID request
					RBUF_READMOVE(connection->input, length + temporary);
					return http_write_response(connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
				}

				//Else: We are writing, initalize fd now
				DEBUG("[#%d] X-Limit of %d found\n", connection->client_sock, limit);

				if (limit >= 0) {
					connection->cache.target.table.limit = limit;
				}
			}
			break;

		case HEADER_XSTART:
			if (REQUEST_IS(connection->method, REQUEST_HTTPGET | REQUEST_CACHE_LEVELTABLE)) {
				int start;
				if (!rbuf_strntol(&connection->input, &start, length)) {
					WARN("Invalid X-Start value provided");

					//This is an INVALID request
					RBUF_READMOVE(connection->input, length + temporary);
					return http_write_response(connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
				}

				//Else: We are writing, initalize fd now
				DEBUG("[#%d] X-Start of %d found\n", connection->client_sock, start);

				if (start >= 0) {
					connection->cache.target.table.start = start;
				}
			}
			break;

		case HEADER_XDELETE:
			if (REQUEST_IS(connection->method, REQUEST_HTTPPURGE | REQUEST_CACHE_LEVELTABLE) && connection->cache.target.table.table != NULL) {
				char* key = (char*)malloc(length);
				rbuf_copyn(&connection->input, key, length);
				cache_entry* entry = db_entry_get_delete(connection->cache.target.table.table, key, length);
				if (entry != NULL) {
					if (db_entry_handle_delete(entry))
					{
						connection->cache.target.table.table = NULL;
					}
					db_entry_deref(entry, false);
				}
			}
			break;
		}


		connection->state = 1;
		CONNECTION_HANDLER(connection,  http_cache_handle_headers);
		RBUF_READMOVE(connection->input, length + temporary);
		return needs_more_read;
	}
	
	return continue_processing;
}


static state_action http_read_version(scache_connection* connection, char* buffer, int& n) {
	if (*buffer == '\n') {
		//TODO: handle version differences
		connection->state = 1;
		CONNECTION_HANDLER(connection,  http_cache_handle_headers);

		RBUF_READMOVE(connection->input, n + 1);
		n = -1;
		return needs_more_read;
	}
	return continue_processing;
}

state_action http_cache_handle_method(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	assert(!connection->epollout && connection->epollin);

	DEBUG("[#%d] Handling HTTP method\n", connection->client_sock);

	//Skip newlines at begining of request (bad clients)
	skip_over_newlines(&connection->input);

	connection->method = 0;

	//Process request line
	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_requeststartmethod(connection, buffer, n));
	return ret;
}

state_action http_cache_handle_url(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	assert(!connection->epollout && connection->epollin);

	DEBUG("[#%d] Handling HTTP url (Stage State: %d)\n", connection->client_sock, connection->state);

	if (connection->state == 0) {
		RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_requeststarturl1(connection, buffer, n));
	}
	else{
		RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_requeststarturl2(connection, buffer, n));
	}
	return ret;
}

state_action http_cache_handle_httpversion(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP version\n", connection->client_sock);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_version(connection, buffer, n));
	if (n != 0) {
		RBUF_READMOVE(connection->input, n);
	}
	return ret;
}

state_action http_cache_handle_eolstats(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP EOL Search, then writing stats\n", connection->client_sock);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_eoltoend(connection, buffer, n, connection->state));
	if (n != 0 && ret == needs_more_read) {
		RBUF_READMOVE(connection->input, n);
	}
	if (ret == registered_write) {
		CONNECTION_HANDLER(connection,  http_respond_stats);
	}

	return ret;
}

state_action http_cache_handle_headers_extract(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP Header extraction\n", connection->client_sock);
	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_header_extraction(connection, buffer, n));
	return ret;
}

state_action http_cache_handle_headers(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP headers (initial: %d)\n", connection->client_sock, connection->state);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_headers(connection, buffer, n, connection->state));

	//make sure we dont 100% fill up
	if (connection->handler == http_cache_handle_headers && rbuf_write_remaining(&connection->input) == 0) {
		return http_write_response(connection, HTTPTEMPLATE_FULLREQUESTTOOLARGE);
	}
	return ret;
}

state_action http_cache_handle_request_body(scache_connection* connection) {
	DEBUG("[#%d] Handling STATE_REQUESTBODY\n", connection->client_sock);
	int max_write = rbuf_read_to_end(&connection->input);
	assert(max_write >= 0);
	int to_write = connection->cache.target.key.end_position - connection->cache.target.key.position;
	assert(to_write >= 0);

	//Limit to the ammount read from socket
	DEBUG("[#%d] Wanting to write %d bytes (max: %d) to fd %d\n", connection->client_sock, to_write, max_write, connection->cache.target.key.fd);
	if (to_write > max_write) {
		to_write = max_write;
	}

	if (to_write != 0) {
		if (connection->is_writing) {
			// Write data
			if(lseek64(connection->cache.target.key.fd, connection->cache.target.key.position, SEEK_SET) == -1) {
				PWARN("[#%d] Failed to seek for PUT", connection->client_sock);
				return http_write_response(connection, HTTPTEMPLATE_FULL404);				
			}
			int read_bytes = write(connection->cache.target.key.fd, RBUF_READ(connection->input), to_write);

			//Handle the bytes written
			DEBUG("[#%d] %d bytes to fd %d at position %u\n", connection->client_sock, read_bytes, connection->cache.target.key.fd, connection->cache.target.key.position);
			RBUF_READMOVE(connection->input, read_bytes);
			connection->cache.target.key.position += read_bytes;
		}
		else{
			max_write = connection->cache.target.key.end_position - connection->cache.target.key.position;
			if(to_write > max_write){
				to_write = max_write;
			}

			RBUF_READMOVE(connection->input, to_write);
			connection->cache.target.key.position += to_write;
		}
	}

	//Check if done
	assert((connection->cache.target.key.end_position - connection->cache.target.key.position) >= 0);
	if (connection->cache.target.key.end_position == connection->cache.target.key.position) {
		//Decrease refs, done with writing
		if (connection->is_writing) {
			DEBUG("[#%d] Completed writing after a total of %d bytes to fd %d\n", connection->client_sock, connection->cache.target.key.position, connection->cache.target.key.fd);
			db_complete_writing(connection->cache.target.key.entry);
			connection->is_writing = false;
		}

		return http_write_response(connection, HTTPTEMPLATE_FULL200OK);
	}

	return continue_processing;
}

void cache_destroy(scache_connection* connection){
	assert(connection->ltype == cache_connection);
	if (REQUEST_IS(connection->method, REQUEST_CACHE_LEVELKEY)) {
		if (connection->is_writing) {
			cache_entry* entry = connection->cache.target.key.entry;
			assert(entry != NULL);
			if (!entry->deleted) {
				db_entry_handle_delete(entry);
			}
			entry->writing = false;
			connection->is_writing = false;
		}
		if (connection->cache.target.key.entry != NULL) {
			db_target_entry_close(&connection->cache.target.key);
			assert(connection->cache.target.key.entry == NULL);
		}
	}
	else if(REQUEST_IS(connection->method, REQUEST_CACHE_LEVELTABLE)) {
		db_table* table = connection->cache.target.table.table;
		if (table != NULL) {
			db_table_close(table);
			connection->cache.target.table.table = NULL;
		}
	}
}