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
#include "scache.h"
#include "http.h"
#include "config.h"
#include "debug.h"
#include "connection.h"
#include "db.h"

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
		buffer;
	}

	//Go over anything from the start
	end = rbuf_read_remaining(rb) - end;
	for (int i = 0; i < end; i++){
		if (*buffer != '\r' && *buffer != '\n'){
			return;
		}
		rb->read_position++;
		buffer;
	}
}

static bool http_write_response(int epfd, cache_connection* connection, int http_template){
	connection->state = STATE_RESPONSEWRITEONLY;
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection_register_write(epfd, connection->client_sock);
	return false;
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
		DEBUG("[#%d] Handling STATE_REQUESTSTART\n", connection->client_sock);

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
				connection->state = STATE_REQUESTSTARTURL;

				//Workout what valid method we have been given (if any)
				if (n == 3 && rbuf_cmpn(&connection->input, "GET", 3) == 0){
					//This is a GET request
					connection->type = REQMETHOD_GET;
					RBUF_READMOVE(connection->input, n + 1);
					return true;
				}
				else if (n == 3 && rbuf_cmpn(&connection->input, "PUT", 3) == 0){
					//This is a PUT request
					connection->type = REQMETHOD_PUT;
					RBUF_READMOVE(connection->input, n + 1);
					return true;
				}

				//Else: This is an INVALID request
				RBUF_READMOVE(connection->input, n + 1);
				return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
			}
		});
		break;

	case STATE_REQUESTSTARTURL:
		DEBUG("[#%d] Handling STATE_REQUESTSTARTURL\n", connection->client_sock);

		RBUF_ITERATE(connection->input, n, buffer, end, {
			DEBUG("%c,", *buffer);
			if (*buffer == ' '){
				//Copy the key from the buffer
				char* key = (char*)malloc(sizeof(char)* (n + 1));
				rbuf_copyn(&connection->input, key, n);
				*(key + n + 1) = 0;//Null terminate the key

				DEBUG("[#%d] Request key: \"%s\" (%d)\n", connection->client_sock, key, n);

				cache_entry* entry;
				mode_t modes = 0;
				//TODO: memcpy always?
				if (connection->type == REQMETHOD_GET){
					//db_entry_get_read will free key if necessary
					entry = db_entry_get_read(key, n);
					connection->state = STATE_REQUESTENDSEARCH;
				}
				else{
					//It is the responsibility of db_entry_get_write to free key if necessary
					entry = db_entry_get_write(key, n);
					connection->state = STATE_REQUESTHEADERS;
					modes = O_CREAT;
				}

				connection->target.position = 0;
				connection->target.entry = entry;
				if (entry != NULL){
					if (IS_SINGLE_FILE(entry)){
						connection->target.fd = db_entry_open(entry, modes);
						connection->target.end_position = entry->data_length;
					}
					else{
						connection->target.fd = db.fd_blockfile;
						connection->target.position = entry->block * BLOCK_LENGTH;
						connection->target.end_position = connection->target.position + entry->data_length;
					}
					entry->refs++;

					connection->output_buffer = http_templates[HTTPTEMPLATE_HEADERS200];
					connection->output_length = http_templates_length[HTTPTEMPLATE_HEADERS200];
				}
				else{
					connection->output_buffer = http_templates[HTTPTEMPLATE_FULL404];
					connection->output_length = http_templates_length[HTTPTEMPLATE_FULL404];
				}
				RBUF_READMOVE(connection->input, n + 1);

				return false;
			}
		});
		break;

	case STATE_REQUESTHEADERS_CONTENTLENGTH:
		DEBUG("[#%d] Handling STATE_REQUESTHEADERS_CONTENTLENGTH\n", connection->client_sock);
		RBUF_ITERATE(connection->input, n, buffer, end, {
			if (*buffer == '\n'){
				//Move read pointer, but keep the newline for newline counting
				RBUF_READMOVE(connection->input, n);

				int content_length;
				if (!rbuf_strntol(&connection->input, &content_length)){
					WARN("Invalid Content-Length value provided");

					//This is an INVALID request
					RBUF_READMOVE(connection->input, 1);
					return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
				}
				else{
					//We are writing, initalize fd now
					DEBUG("[#%d] Content-Length of %d found\n", connection->client_sock, content_length);
					db_entry_write_init(&connection->target, content_length);
					connection->state = STATE_REQUESTHEADERS;
					return true;
				}
			}
		});
		break;

	case STATE_REQUESTHEADERS:
		DEBUG("[#%d] Handling STATE_REQUESTHEADERS\n", connection->client_sock);
		temporary = 1;
		RBUF_ITERATE(connection->input, n, buffer, end, {
			if (*buffer == ':'){
				DEBUG("[#%d] Found header of length %d\n", connection->client_sock, n);
				if (n == 14 && rbuf_cmpn(&connection->input, "Content-Length", 14) == 0){
					DEBUG("[#%d] Found Content-Length header\n", connection->client_sock);
					RBUF_READMOVE(connection->input, n + 1);
					connection->state = STATE_REQUESTHEADERS_CONTENTLENGTH;
					return true;
				}
			}
			else if (*buffer == '\n'){
				temporary++;
				if (temporary == 2){
					RBUF_READMOVE(connection->input, n + 1);
					connection->state = STATE_REQUESTBODY;
					return true;
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
		if (rbuf_write_remaining(&connection->input) != 0){
			RBUF_READMOVE(connection->input, n - 2);
		}
		else{
			RBUF_READMOVE(connection->input, n + 1);
			return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
		}

		break;
	case STATE_REQUESTENDSEARCH:
		DEBUG("[#%d] Handling STATE_REQUESTENDSEARCH\n", connection->client_sock);

		//Start with one new line (the new line that caused this state change)
		temporary = 1;

		//Search for two newlines (unix or windows)
		RBUF_ITERATE(connection->input, n, buffer, end, {
			if (*buffer == '\n'){
				temporary++;
				if (temporary == 2){
					RBUF_READMOVE(connection->input, n + 1);

					if (connection->target.entry != NULL){
						connection->state = STATE_RESPONSESTART;
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
		//Go back 3 bytes, might go back too far - but thats ok we dont have that short headers
		if (rbuf_write_remaining(&connection->input) != 0){
			RBUF_READMOVE(connection->input, n - 2);
		}
		else{
			RBUF_READMOVE(connection->input, n + 1);
			return http_write_response(epfd, connection, HTTPTEMPLATE_FULLINVALIDMETHOD);
		}

		break;

	case STATE_REQUESTBODY:
		DEBUG("[#%d] Handling STATE_REQUESTBODY\n", connection->client_sock);
		int max_write = rbuf_read_to_end(&connection->input);
		assert(max_write >= 0);
		int to_write = connection->target.entry->data_length - connection->target.position;
		assert(to_write >= 0);

		//Limit to the ammount read from socket
		DEBUG("[#%d] Wanting to write %d bytes (max: %d) to fd %d\n", connection->client_sock, to_write, max_write, connection->target.fd);
		if (to_write > max_write){
			to_write = max_write;
		}

		if (to_write != 0){
			// Write data
			int read_bytes = write(connection->target.fd, RBUF_READ(connection->input), to_write);

			//Handle the bytes written
			DEBUG("[#%d] %d bytes to fd %d\n", connection->client_sock, read_bytes, connection->target.fd);
			RBUF_READMOVE(connection->input, read_bytes);
			connection->target.position += read_bytes;
		}

		//Check if done
		assert((connection->target.entry->data_length - connection->target.position) >= 0);
		if ((connection->target.entry->data_length - connection->target.position) == 0){
			connection->output_buffer = http_templates[HTTPTEMPLATE_FULL200OK];
			connection->output_length = http_templates_length[HTTPTEMPLATE_FULL200OK];
			connection->state = STATE_RESPONSEWRITEONLY;
			connection_register_write(epfd, connection->client_sock);

			//Decrease refs, done with writing
			connection->target.entry->writing = false;
			connection->target.entry->refs--;

			//No longer using an entry
			connection->target.entry = NULL;
			connection->target.position = 0;
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
		temp = snprintf(misc_buffer, 4096, "Content-Length: %d\r\n", connection->target.entry->data_length);

		connection->output_buffer_free = (char*)malloc(temp);
		memcpy(connection->output_buffer_free, misc_buffer, temp);
		connection->output_buffer = connection->output_buffer_free;
		connection->output_length = temp;
		connection->state = STATE_RESPONSEEND;
		break;
	case STATE_RESPONSEEND:
		DEBUG("[#%d] Handling STATE_RESPONSEEND\n", fd);
		connection->output_buffer = http_templates[HTTPTEMPLATE_DBLNEWLINE];
		connection->output_length = http_templates_length[HTTPTEMPLATE_DBLNEWLINE];
		connection->state = STATE_RESPONSEBODY;
		break;
	case STATE_RESPONSEBODY:
		DEBUG("[#%d] Handling STATE_RESPONSEBODY\n", fd);
		//The number of bytes to read
		temp = connection->target.entry->data_length - connection->target.position;
		DEBUG("[#%d] To send %d bytes to the socket (len: %d, pos: %d)", fd, temp, connection->target.entry->data_length, connection->target.position);
		assert(temp >= 0);
		if (temp != 0){
			off_t pos = connection->target.position;
			int bytes_sent = sendfile(fd, connection->target.fd, &pos, temp);
			if (bytes_sent < 0){
				PFATAL("Error sending bytes with sendfile");
			}
			DEBUG("[#%d] Sendfile sent %d bytes from position %d", fd, bytes_sent, connection->target.position);
			connection->target.position += bytes_sent;
			DEBUG("[#%d] Position is now %d", fd, connection->target.position);
		}


		assert(connection->target.position <= connection->target.entry->data_length);
		if (connection->target.position == connection->target.entry->data_length){
			connection->target.entry->refs--;
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