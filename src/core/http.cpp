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

int http_templates_length[NUMBER_OF_HTTPTEMPLATE];
char misc_buffer[4096];

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
		rb->read_position++;
		buffer++;
	}

	//Go over anything from the start
	end = rbuf_read_remaining(rb) - end;
	for (int i = 0; i < end; i++){
		if (*buffer != '\r' && *buffer != '\n'){
			return;
		}
		rb->read_position++;
		buffer++;
	}
}

static bool http_write_response_after_eol(int epfd, cache_connection* connection, int http_template){
	connection->state = STATE_HTTPEOLWRITE;
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	return false;
}

static bool http_write_response(int epfd, cache_connection* connection, int http_template){
	connection->state = STATE_RESPONSEWRITEONLY;
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection_register_write(epfd, connection->client_sock);
	return false;
}

static bool http_key_lookup(cache_connection* connection, int n, int epfd){
	//Key request
	//Copy the key from the buffer
	char* key = (char*)malloc(sizeof(char)* (n + 1));
	rbuf_copyn(&connection->input, key, n);
	*(key + n + 1) = 0;//Null terminate the key

	DEBUG("[#%d] Request key: \"%s\" (%d)\n", connection->client_sock, key, n);

	struct cache_entry* entry;
	mode_t modes = 0;
	//TODO: memcpy always?
	if (REQUEST_IS(connection->type, REQUEST_HTTPGET)){
		//db_entry_get_read will free key if necessary
		entry = db_entry_get_read(connection->target.table.table, key, n);
	}
	else if (REQUEST_IS(connection->type, REQUEST_HTTPPUT)){
		//It is the responsibility of db_entry_get_write to free key if necessary
		entry = db_entry_get_write(connection->target.table.table, key, n);
		modes = O_CREAT;
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
	connection->state = STATE_HTTPVERSION;

	connection->target.cache.position = 0;
	connection->target.cache.entry = entry;
	if (entry != NULL){
		if (IS_SINGLE_FILE(entry)){
			connection->target.cache.fd = db_entry_open(entry, modes);
		}
		else{
			connection->target.cache.fd = db.fd_blockfile;
			connection->target.cache.position = entry->block * BLOCK_LENGTH;
		}
		if (connection->type == REQUEST_GETKEY){
			connection->target.cache.end_position = connection->target.cache.position + entry->data_length;
		}

		if (connection->output_buffer == NULL){
			connection->output_buffer = http_templates[HTTPTEMPLATE_HEADERS200];
			connection->output_length = http_templates_length[HTTPTEMPLATE_HEADERS200];
		}
	}
	else{
		connection->output_buffer = http_templates[HTTPTEMPLATE_FULL404];
		connection->output_length = http_templates_length[HTTPTEMPLATE_FULL404];
	}

	return true;
}

/*
Handle read data for the connection

return true to signal intent to process more data
*/
bool http_read_handle_state(int epfd, cache_connection* connection){
	//Variable used in RBUF_ITERATE and elsewhere
	//to represent a pointer to the currently processing
	//part of the received data
	char* buffer;

	//Temporary variable used by RBUF_ITERATE to
	//store the maximum possible to iterate
	int end;

	//Variable to contain the number of chars
	//iterated over.
	int n;

	// Variable for misc usage inside states
	int temporary;

	//State machine
	switch (connection->state){
	case STATE_REQUESTSTARTMETHOD:
		DEBUG("[#%d] Handling STATE_REQUESTSTARTMETHOD\n", connection->client_sock);

		//Skip newlines at begining of request (bad clients)
		skip_over_newlines(&connection->input);

		//Process request line
		RBUF_ITERATE(connection->input, n, buffer, end, {
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
				connection->state = STATE_REQUESTSTARTURL1;

				//Workout what valid method we have been given (if any)
				if (n == 3 && rbuf_cmpn(&connection->input, "GET", 3) == 0){
					//This is a GET request
					connection->type = REQUEST_HTTPGET;
					RBUF_READMOVE(connection->input, n + 1);
					return true;
				}
				else if (n == 3 && rbuf_cmpn(&connection->input, "PUT", 3) == 0){
					//This is a PUT request
					connection->type = REQUEST_HTTPPUT;
					RBUF_READMOVE(connection->input, n + 1);
					return true;
				}
				else if (n == 6 && rbuf_cmpn(&connection->input, "DELETE", 6) == 0){
					//This is a DELETE request
					connection->type = REQUEST_HTTPDELETE;
					RBUF_READMOVE(connection->input, n + 1);
					return true;
				}

				//Else: This is an INVALID request
				RBUF_READMOVE(connection->input, n + 1);
				return http_write_response_after_eol(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
			}
		});
		break;

	case STATE_REQUESTSTARTURL1:
		DEBUG("[#%d] Handling STATE_REQUESTSTARTURL\n", connection->client_sock);

		RBUF_ITERATE(connection->input, n, buffer, end, {
			//Assert: first char is a / (start of URL)
			assert(n!=0 || *buffer=='/');


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

				connection->state = STATE_REQUESTSTARTURL2;

				RBUF_READMOVE(connection->input, n);
				return true;
			}else if (*buffer == ' '){
				//Table command
				//URL: table

				if (REQUEST_IS(connection->type, REQUEST_HTTPGET)){

				}
				else if (REQUEST_IS(connection->type, REQUEST_HTTPDELETE)){

				}
				else
				{
					return http_write_response_after_eol(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
				}
			}
		});
		break;

	case STATE_REQUESTSTARTURL2:
		RBUF_ITERATE(connection->input, n, buffer, end, {
			if (*buffer == ' '){
				temporary = http_key_lookup(connection, n, epfd);
				RBUF_READMOVE(connection->input, n + 1);

				return temporary;
			}
		});
		break;

	case STATE_HTTPVERSION:
		DEBUG("[#%d] Handling STATE_HTTPVERSION\n", connection->client_sock);

		RBUF_ITERATE(connection->input, n, buffer, end, {
			if (*buffer == '\n'){
				//TODO: handle version differences
				if (connection->type == REQUEST_GETKEY){
					connection->state = STATE_REQUESTENDSEARCH;
				}
				else if (connection->type == REQUEST_PUTKEY){
					connection->state = STATE_REQUESTHEADERS;
				}
				else if (connection->type == REQUEST_DELETEKEY){
					connection->state = STATE_REQUESTENDSEARCH;
				}
				else{
					FATAL("Unknown connection type state\r\n");
				}
				RBUF_READMOVE(connection->input, n + 1);
				return true;
			}
		});
		if (n != 0){
			RBUF_READMOVE(connection->input, n);
		}
		break;

	case STATE_HTTPEOLWRITE:
		DEBUG("[#%d] Handling STATE_HTTPEOLWRITE\n", connection->client_sock);

		RBUF_ITERATE(connection->input, n, buffer, end, {
			if (*buffer == '\n'){
				connection->state = STATE_RESPONSEWRITEONLY;
				connection_register_write(epfd, connection->client_sock);
				return false;
			}
		});
		if (n != 0){
			RBUF_READMOVE(connection->input, n);
		}
		break;

	case STATE_REQUESTHEADERS_CONTENTLENGTH:
		DEBUG("[#%d] Handling STATE_REQUESTHEADERS_CONTENTLENGTH\n", connection->client_sock);
		temporary = 0;
		RBUF_ITERATE(connection->input, n, buffer, end, {
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

				int content_length;
				if (!rbuf_strntol(&connection->input, &content_length, n)){
					WARN("Invalid Content-Length value provided");

					//This is an INVALID request
					RBUF_READMOVE(connection->input, n + temporary);
					return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
				}

				//Else: We are writing, initalize fd now
				DEBUG("[#%d] Content-Length of %d found\n", connection->client_sock, content_length);
				db_entry_write_init(&connection->target.cache, content_length);

				if (IS_SINGLE_FILE(connection->target.cache.entry)){
					connection->target.cache.end_position = connection->target.cache.entry->data_length;
				}
				else{
					connection->target.cache.end_position = connection->target.cache.position + connection->target.cache.entry->data_length;
				}
				connection->state = STATE_REQUESTHEADERS;
				RBUF_READMOVE(connection->input, n + temporary);
				return true;
			}
			else{
				temporary = 1;
			}
		});
		break;

	case STATE_REQUESTHEADERS_XTTL:
		DEBUG("[#%d] Handling STATE_REQUESTHEADERS_XTTL\n", connection->client_sock);
		temporary = 0;
		RBUF_ITERATE(connection->input, n, buffer, end, {
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
					connection->target.cache.entry->expires = ttl + current_time.tv_sec;
				}
								
				connection->state = STATE_REQUESTHEADERS;
				RBUF_READMOVE(connection->input, n + temporary);
				return true;
			}
			else{
				temporary = 1;
			}
		});
		break;

	case STATE_REQUESTHEADERS_ZERO:
	case STATE_REQUESTHEADERS:
		DEBUG("[#%d] Handling %s\n", connection->client_sock, (connection->state == STATE_REQUESTHEADERS) ? "STATE_REQUESTHEADERS" : "STATE_REQUESTHEADERS_ZERO");
		temporary = (connection->state == STATE_REQUESTHEADERS);

		RBUF_ITERATE(connection->input, n, buffer, end, {
			if (*buffer == ':'){
				DEBUG("[#%d] Found header of length %d\n", connection->client_sock, n);
				if (n == 14 && rbuf_cmpn(&connection->input, "Content-Length", 14) == 0){
					DEBUG("[#%d] Found Content-Length header\n", connection->client_sock);
					RBUF_READMOVE(connection->input, n + 1);
					connection->state = STATE_REQUESTHEADERS_CONTENTLENGTH;
					return true;
				}
				if (n == 5 && rbuf_cmpn(&connection->input, "X-Ttl", 5) == 0){
					DEBUG("[#%d] Found X-Ttl header\n", connection->client_sock);
					RBUF_READMOVE(connection->input, n + 1);
					connection->state = STATE_REQUESTHEADERS_XTTL;
					return true;
				}
			}
			else if (*buffer == '\n'){
				temporary++;
				if (temporary == 2){
					RBUF_READMOVE(connection->input, n + 1);

					//TODO: better
					if (connection->target.cache.entry->data_length != 0){
						//TODO: handle missing content-length?
						connection->state = STATE_REQUESTBODY;
						return true;
					}
					else{
						return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
					}
				}

				//Move pointers to next record
				RBUF_READMOVE(connection->input, n + 1);
				n = -1;
			}
			else if (*buffer != '\r'){
				temporary = 0;
			}
		});

		//Couldnt find the end in this 4kb chunk
		//Go back 3 bytes, might go back too far - but thats ok we dont have that short headers
		RBUF_READMOVE(connection->input, n);
		if (rbuf_write_remaining(&connection->input) == 0){
			return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
		}
		else{
			//State depends on finishing state of temporary variable
			connection->state = (temporary == 0) ? STATE_REQUESTHEADERS_ZERO : STATE_REQUESTHEADERS;
		}


		break;
	case STATE_REQUESTENDSEARCH:
	case STATE_REQUESTENDSEARCH_ZERO:
		DEBUG("[#%d] Handling %s\n", connection->client_sock, (connection->state == STATE_REQUESTENDSEARCH)?"STATE_REQUESTENDSEARCH":"STATE_REQUESTENDSEARCH_ZERO");

		//Start with one new line (the new line that caused this state change)
		temporary = (connection->state == STATE_REQUESTENDSEARCH);

		//Search for two newlines (unix or windows)
		RBUF_ITERATE(connection->input, n, buffer, end, {
			if (*buffer == '\n'){
				temporary++;
				if (temporary == 2){
					RBUF_READMOVE(connection->input, n + 1);

					if (connection->target.cache.entry != NULL){
						if (connection->type == REQUEST_DELETEKEY){
							connection->state = STATE_RESPONSEWRITEONLY;
							db_entry_handle_delete(connection->target.cache.entry);
							db_entry_close(&connection->target.cache);
						}
						else{
							connection->state = STATE_RESPONSESTART;
						}
					}
					else{
						connection->state = STATE_RESPONSEWRITEONLY;
					}
					connection_register_write(epfd, connection->client_sock);
					return false;
				}
			}
			else if (*buffer != '\r'){
				temporary = 0;
			}
		});

		//Couldnt find the end in this 4kb chunk
		//Maximum request size == buffer size
		RBUF_READMOVE(connection->input, n);
		if (rbuf_write_remaining(&connection->input) == 0){
			return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
		}
		else{
			//State depends on finishing state of temporary variable
			connection->state = (temporary == 0) ? STATE_REQUESTENDSEARCH_ZERO : STATE_REQUESTENDSEARCH;
		}

		break;

	case STATE_REQUESTBODY:
		DEBUG("[#%d] Handling STATE_REQUESTBODY\n", connection->client_sock);
		int max_write = rbuf_read_to_end(&connection->input);
		assert(max_write >= 0);
		int to_write = connection->target.cache.end_position - connection->target.cache.position;
		assert(to_write >= 0);

		//Limit to the ammount read from socket
		DEBUG("[#%d] Wanting to write %d bytes (max: %d) to fd %d\n", connection->client_sock, to_write, max_write, connection->target.cache.fd);
		if (to_write > max_write){
			to_write = max_write;
		}

		if (to_write != 0){
			// Write data
			lseek(connection->target.cache.fd, connection->target.cache.position, SEEK_SET);
			int read_bytes = write(connection->target.cache.fd, RBUF_READ(connection->input), to_write);

			//Handle the bytes written
			DEBUG("[#%d] %d bytes to fd %d\n", connection->client_sock, read_bytes, connection->target.cache.fd);
			RBUF_READMOVE(connection->input, read_bytes);
			connection->target.cache.position += read_bytes;
		}

		//Check if done
		assert((connection->target.cache.end_position - connection->target.cache.position) >= 0);
		if (connection->target.cache.end_position == connection->target.cache.position){
			connection->output_buffer = http_templates[HTTPTEMPLATE_FULL200OK];
			connection->output_length = http_templates_length[HTTPTEMPLATE_FULL200OK];
			connection->state = STATE_RESPONSEWRITEONLY;
			connection_register_write(epfd, connection->client_sock);

			//Decrease refs, done with writing
			connection->target.cache.entry->writing = false;
			db_entry_close(&connection->target.cache);
		}
		break;
	}

	return false;
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
	int fd = connection->client_sock;
	int temp;

	switch (connection->state){
	case STATE_RESPONSESTART:
		//Before using this state, ensure the template is already set
		//Psudeo state, just proceed onwards - data has been written
	case STATE_RESPONSEHEADER_CONTENTLENGTH:
		DEBUG("[#%d] Handling STATE_RESPONSEHEADER_CONTENTLENGTH\n", fd);
		//Returns the number of chars put into the buffer
		temp = snprintf(misc_buffer, 4096, "Content-Length: %d\r\n", connection->target.cache.entry->data_length);

		connection->output_buffer_free = (char*)malloc(temp);
		memcpy(connection->output_buffer_free, misc_buffer, temp);
		connection->output_buffer = connection->output_buffer_free;
		connection->output_length = temp;
		connection->state = STATE_RESPONSEEND;
		break;
	case STATE_RESPONSEEND:
		DEBUG("[#%d] Handling STATE_RESPONSEEND\n", fd);
		connection->output_buffer = http_templates[HTTPTEMPLATE_NEWLINE];
		connection->output_length = http_templates_length[HTTPTEMPLATE_NEWLINE];
		connection->state = STATE_RESPONSEBODY;
		break;
	case STATE_RESPONSEBODY:
		DEBUG("[#%d] Handling STATE_RESPONSEBODY\n", fd);
		//The number of bytes to read
		temp = connection->target.cache.end_position - connection->target.cache.position;
		DEBUG("[#%d] To send %d bytes to the socket (len: %d, pos: %d)\n", fd, temp, connection->target.cache.entry->data_length, connection->target.cache.position);
		assert(temp >= 0);
		if (temp != 0){
			off_t pos = connection->target.cache.position;
			int bytes_sent = sendfile(fd, connection->target.cache.fd, &pos, temp);
			if (bytes_sent < 0){
				PFATAL("Error sending bytes with sendfile");
			}
			DEBUG("[#%d] Sendfile sent %d bytes from position %d\n", fd, bytes_sent, connection->target.cache.position);
			connection->target.cache.position += bytes_sent;
			DEBUG("[#%d] Position is now %d\n", fd, connection->target.cache.position);
		}


		assert(connection->target.cache.position <= connection->target.cache.end_position);
		if (connection->target.cache.position == connection->target.cache.end_position){
			db_entry_close(&connection->target.cache);
			connection->state = STATE_REQUESTSTARTMETHOD;
			connection_register_read(epfd, fd);
		}
		break;
	case STATE_RESPONSEWRITEONLY:
		DEBUG("[#%d] Handling STATE_RESPONSEWRITEONLY\n", fd);
		//Static response, after witing, read next request
		connection->state = STATE_REQUESTSTARTMETHOD;
		connection_register_read(epfd, fd);
		break;
	}

	return false;
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