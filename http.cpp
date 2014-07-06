#include <assert.h>
#include "scache.h"
#include "http.h"
#include "config.h"
#include "debug.h"
#include "connection.h"

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
	case STATE_REQUESTSTART:
		DEBUG("[#%d] Handling STATE_REQUESTSTART\n", fd);
		start = buffer = connection->input_buffer + connection->input_read_position;
		end = connection->input_buffer + connection->input_buffer_write_position;
		while (buffer < end){
			if (*buffer == ' '){
				int method_len = buffer - start;
				DEBUG("[#%d] Found first space seperator, len: %d\n", fd, method_len);
				if (method_len == 3 && strncmp(start, "GET", 3) == 0){
					connection->type = REQMETHOD_GET;
					connection->state = STATE_REQUESTENDSEARCH;
					connection->input_read_position += 4;
					return true;
				}
				else if (method_len == 3 && strncmp(start, "PUT", 3) == 0){
					connection->type = REQMETHOD_PUT;
					connection->state = STATE_REQUESTHEADERS;
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
					db_write_init(connection->target.entry, content_length);
				}
				newlines++;
				if (newlines == 2){
					connection->input_read_position = buffer - connection->input_buffer + 1;
					connection->state = STATE_REQUESTEND;
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
		if (!continue_loop){
			connection->input_read_position = buffer - connection->input_buffer - 3;
		}
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
	case STATE_REQUESTEND:
		break;
	case STATE_REQUESTENDSEARCH:
		break;
	case STATE_REQUESTBODY:
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
	}

	return false;
}

/*
Handle the writing of data to the connection

return true to signal intent to send more data
*/
bool http_write_handle_state(int epfd, cache_connection* connection){
	switch (connection->state){
	case STATE_RESPONSESTART:
		break;
	case STATE_RESPONSEHEADER_CONTENTLENGTH:
		break;
	case STATE_RESPONSEEND:
		break;
	case STATE_RESPONSEBODY:
		break;
	case STATE_RESPONSEWRITEONLY:
		break;
	}
}

/*
Handle the connection (Write Event)

return true to close the connection
*/
bool http_write_handle(int epfd, cache_connection* connection){

}


void http_templates_init(){
	for (int i = 0; i < NUMBER_OF_HTTPTEMPLATE; i++){
		http_templates_length[i] = strlen(http_templates[i]);
	}
}