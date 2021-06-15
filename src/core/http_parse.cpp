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


void skip_over_newlines(struct read_buffer* rb) {
	char* buffer;
	int end = rbuf_read_to_end(rb);

	//Nothing to skip
	if (end == 0) {
		return;
	}

	//Iterate over buffer until the end of the buffer
	buffer = RBUF_READPTR(rb);
	for (int i = 0; i < end; i++) {
		if (*buffer != '\r' && *buffer != '\n') {
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
	for (int i = 0; i < end; i++) {
		if (*buffer != '\r' && *buffer != '\n') {
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


state_action http_read_eoltoend(scache_connection* connection, char* buffer, int n, uint32_t& temporary) {
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
	if (target == current) {
		// register a handler to write the output (and then the request is over)
		connection->state = 0;
		CONNECTION_HANDLER(connection,  http_respond_writeonly);
		bool res = connection_register_write(connection->client_sock);

		if (!res) {
			return close_connection;
		}

		if (n != 0) {
			RBUF_READMOVE(connection->input, n);
		}
		return registered_write;
	}
	return continue_processing;
}



state_action http_handle_eolwritetoend(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP EOL Search, then writing state\n", connection->client_sock);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_eoltoend(connection, buffer, n, connection->state));
	if (n != 0 && ret == needs_more) {
		RBUF_READMOVE(connection->input, n);
	}

	return ret;
}

state_action http_discard_input(scache_connection* connection) {
	//todo discard from readbuffer
	connection_register_read(connection->client_sock);
	rbuf_read_to_end(&connection->input);
	return continue_processing;
}