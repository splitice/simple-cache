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


char misc_buffer[4096];

/*
Handle read data for the connection

return true to signal intent to process more data
*/
bool http_read_handle_state(int epfd, cache_connection* connection){
	int fd = connection->client_sock;
	char* buffer;
	char* start;
	char* end;

	switch (connection->state){
	case STATE_REQUESTSTARTMETHOD:
		DEBUG("[#%d] Handling STATE_REQUESTSTART\n", fd);
		start = buffer = connection->input_buffer + connection->input_read_position;
		end = connection->input_buffer + connection->input_buffer_write_position;
		while (buffer < end){
			if (*buffer == ' '){
				int method_len = buffer - start;
				DEBUG("[#%d] Found first space seperator, len: %d\n", fd, method_len);
				if (method_len == 3 && strncmp(start, "GET", 3) == 0){
					connection->type = REQMETHOD_GET;
					connection->state = STATE_REQUESTSTARTURL;
					connection->input_read_position += 4;
					return true;
				}
				else if (method_len == 3 && strncmp(start, "PUT", 3) == 0){
					connection->type = REQMETHOD_PUT;
					connection->state = STATE_REQUESTSTARTURL;
					connection->input_read_position += 4;
					return true;
				}
				else{
					connection->state = STATE_RESPONSEWRITEONLY;
					connection->output_buffer = http_templates[HTTPTEMPLATE_FULLINVALIDMETHOD];
					connection->output_length = http_templates_length[HTTPTEMPLATE_FULLINVALIDMETHOD];
					connection_register_read(epfd, fd);
					return false;
				}
			}
			buffer++;
		}
		break;

	case STATE_REQUESTSTARTURL:
		DEBUG("[#%d] Handling STATE_REQUESTSTARTURL\n", fd);
		start = buffer = (char*)(connection->input_buffer + connection->input_read_position);
		end = (char*)(connection->input_buffer + connection->input_buffer_write_position);

		while (buffer < end){
			if (*buffer == ' '){
				int length = buffer - start - 1;
				*buffer = 0;//Null terminate the key
				DEBUG("[#%d] Request key: \"%s\"\n", fd, start);
				cache_entry* entry;
				if (connection->type == REQMETHOD_GET){
					entry = db_entry_get_read(start, length);
					connection->state = STATE_REQUESTENDSEARCH;
				}
				else{
					entry = db_entry_get_write(start, length);
					connection->state = STATE_REQUESTHEADERS;
				}

				connection->target.entry = entry;
				if (entry != NULL){
					if (IS_SINGLE_FILE(entry)){
						connection->target.fd = open_cache_entry(entry);
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
				connection->input_read_position += length;
				return 1;
			}
			buffer++;
		}
		break;

	case STATE_REQUESTHEADERS:
		DEBUG("[#%d] Handling STATE_REQUESTHEADERS\n", fd);
		start = buffer = connection->input_buffer + connection->input_read_position;
		end = (char*)(connection->input_buffer + connection->input_buffer_write_position);
		int newlines = 0;
		int header_type = 0;
		while (buffer < end){
			if (*buffer == ':'){
				int header_length = buffer - start;

				DEBUG("[#%d] Found header of length %d\n", fd, header_length);
				if (header_length == 14 && strncmp(start, "Content-Length", 14) == 0){
					DEBUG("[#%d] Found Content-Length header\n", fd);
					header_type = HEADER_CONTENTLENGTH;
				}

				//Skip spaces
				do {
					buffer++;
				} while (buffer < end && *buffer == ' ');

				start = buffer;

				//Skip normal increment
				continue;
			}
			else if (*buffer == '\n'){
				if (header_type == HEADER_CONTENTLENGTH){
					int content_length = strtol(start, NULL, 10);
					if (content_length == 0){
						WARN("Invalid Content-Length value provided");
					}
					db_entry_write_init(connection->target.entry, content_length);
				}
				newlines++;
				if (newlines == 2){
					connection->input_read_position = buffer - connection->input_buffer + 1;
					connection->state = STATE_RESPONSESTART;
					connection_register_write(epfd, fd);
					return true;
				}
				start = buffer + 1;
			}
			else if (*buffer != '\r'){
				newlines = 0;
			}
			buffer++;
		}

		//Couldnt find the end in this 4kb chunk
		connection->input_read_position = buffer - connection->input_buffer - 3;
		
		//TODO: handle request too long
		/*else{
			if ((buffer - connection->input_buffer) == connection->input_buffer_write_position){
				connection->input_read_position = 0;
				connection->input_buffer_write_position = 0;
			}
			if (connection->type == REQTYPE_READ)
			{
				//Dont actually continue loop,
				//instead register for write
				DEBUG("[#%d] End of headers, now we can write\n", fd);
				continue_loop = 0;

				register_handle_write(fd, epfd);
			}
		}*/
		break;
	case STATE_REQUESTENDSEARCH:
		int newlines = 0;
		while (buffer < end){
			if (*buffer == '\n'){
				newlines++;
				if (newlines == 2){
					connection->input_read_position = buffer - connection->input_buffer + 1;
					connection->state = STATE_RESPONSESTART;
					connection_register_write(epfd, fd);
					return true;
				}
			}
			else if (*buffer != '\r'){
				newlines = 0;
			}
			buffer++;
		}

		//Couldnt find the end in this 4kb chunk
		connection->input_read_position = buffer - connection->input_buffer - 3;

		break;
	case STATE_REQUESTBODY:
		cache_target* target = &connection->target;
		int to_write = connection->input_buffer_write_position - connection->input_read_position;
		int read_bytes = write(target->fd, connection->input_buffer + connection->input_read_position, to_write);
		connection->input_read_position += read_bytes;

		if (connection->input_read_position == target->entry->data_length){
			connection->output_buffer = http_templates[HTTPTEMPLATE_FULL200OK];
			connection->output_length = http_templates_length[HTTPTEMPLATE_FULL200OK];
			connection->state = STATE_RESPONSEWRITEONLY;
			connection_register_write(epfd, fd);
			connection->target.entry->refs--;
		}
		break;
	}
}

/*
Handle the connection (Read Event)

return true to close the connection
*/
bool http_read_handle(int epfd, cache_connection* connection){
	int num;
	int fd = connection->client_sock;

	//Read from socket
	int to_read = BLOCK_LENGTH - connection->input_buffer_write_position;
	assert(to_read > 0);
	num = read(fd, connection->input_buffer + connection->input_buffer_write_position, to_read);

	if (num <= 0){
		DEBUG("A socket error occured while reading: %d", num);
		return true;
	}

	connection->input_buffer_write_position += num;

	bool run;
	do {
		run = http_read_handle_state(epfd, connection);
	} while (run);

	return false;
}

/*
Handle the writing of data to the connection

return true to signal intent to send more data
*/
bool http_write_handle_state(int epfd, cache_connection* connection){
	int fd = connection->client_sock;

	switch (connection->state){
	case STATE_RESPONSESTART:
		//Before using this state, ensure the template is already set
		//Psudeo state, just proceed onwards - data has been written
		connection->state = STATE_RESPONSEHEADER_CONTENTLENGTH;
		break;
	case STATE_RESPONSEHEADER_CONTENTLENGTH:
		DEBUG("[#%d] Handling STATE_RESPONSEHEADER_CONTENTLENGTH\n", fd);
		int chars = snprintf(misc_buffer, 4096, "Content-Length: %d\r\n", connection->target.entry->data_length);

		char* content_length = malloc(chars * sizeof(char));
		memcpy(content_length, misc_buffer, chars);

		connection->output_buffer = content_length;
		connection->output_length = chars;
		connection->output_buffer_free = chars;
		break;
	case STATE_RESPONSEEND:
		DEBUG("[#%d] Handling STATE_RESPONSEEND\n", fd);
		connection->output_buffer = http_templates[HTTPTEMPLATE_DBLNEWLINE];
		connection->output_length = http_templates_length[HTTPTEMPLATE_DBLNEWLINE];
		connection->state = STATE_RESPONSEBODY;
		connection_register_write(epfd, fd);
		break;
	case STATE_RESPONSEBODY:
		cache_target* target = &connection->target;
		size_t to_read = target->end_position - target->position;
		int bytes_sent = sendfile(fd, target->fd, &target->position, to_read);
		target->position += bytes_sent;
		if (target->position == target->end_position){
			connection->target.entry->refs--;
			connection->state = STATE_REQUESTSTARTMETHOD;
			connection_register_read(epfd, fd);
		}
		break;
	case STATE_RESPONSEWRITEONLY:
		//Static response, after witing, read next request
		connection->state = STATE_REQUESTSTARTMETHOD;
		connection_register_read(epfd, fd);
		break;
	}
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


void http_templates_init(){
	for (int i = 0; i < NUMBER_OF_HTTPTEMPLATE; i++){
		http_templates_length[i] = strlen(http_templates[i]);
	}
}