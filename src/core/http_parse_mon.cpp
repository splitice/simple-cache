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
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
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

#define MONITORING_DEFAULT_INTERVAL 5
static char monitoring_strings[0x10000][7] = {};
static uint8_t monitoring_lens[0x10000] = {};

static state_action http_write_response_after_eol(scache_connection* connection, int http_template) {
	CONNECTION_HANDLER(connection, http_handle_eolwritetoend);
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 0;
	return needs_more;
}

static state_action http_write_response(scache_connection* connection, int http_template) {
	CONNECTION_HANDLER(connection,  http_respond_writeonly);
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 0;
	bool res = connection_register_write(connection->client_sock);
	return res?registered_write:close_connection;
}


static state_action http_headers_response_after_eol(scache_connection* connection, int http_template) {
	CONNECTION_HANDLER(connection,  http_handle_eolwritetoend);
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 2;
	return needs_more;
}




state_action http_respond_writecount_counter(scache_connection* connection) {
	DEBUG("[#%d] Sending for count\n", connection->client_sock);
	//Static response, after witing, read next request
	http_cleanup(connection);
	CONNECTION_HANDLER(connection,  http_cache_handle_method);
	bool res = http_register_read(connection);
	return res ? continue_processing : close_connection;
}
state_action http_respond_writecount_starting(scache_connection* connection) {
	DEBUG("[#%d] Starting write count\n", connection->client_sock);
	//Static response, after witing, read next request
	CONNECTION_HANDLER(connection,  http_mon_handle_method);
	return continue_processing;
}

static state_action http_respond_start_to_count(scache_connection* connection) {
	DEBUG("[#%d] Ready to start to count \n", connection->client_sock);
	monitoring_add(connection);
	CONNECTION_HANDLER(connection,  http_respond_cleanupafterwrite);
	return continue_processing;
}

state_action http_mon_read_eol_inital(scache_connection* connection, char* buffer, int n, uint32_t& temporary) {
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

		connection->output_buffer = http_templates[HTTPTEMPLATE_MON_STREAM];
		connection->output_length = http_templates_length[HTTPTEMPLATE_MON_STREAM];

		CONNECTION_HANDLER(connection,  http_respond_start_to_count);
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



state_action http_handle_mon_eolwrite_initial(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP EOL Search, then writing header text\n", connection->client_sock);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_mon_read_eol_inital(connection, buffer, n, connection->state));
	if (n != 0 && ret == needs_more) {
		RBUF_READMOVE(connection->input, n);
	}

	return ret;
}




static state_action http_headers_response_count(scache_connection* connection, int http_template) {
	connection->state = 2;
	CONNECTION_HANDLER(connection,  http_handle_mon_eolwrite_initial);
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	return needs_more;
}

static inline state_action http_read_requeststartmethod_mon(scache_connection* connection, char* buffer, int n) {
	//Check if this is never going to be valid, too long
	if (n > LONGEST_REQMETHOD) {
		RBUF_READMOVE(connection->input, n + 1);
		return http_write_response_after_eol(connection, HTTPTEMPLATE_FULLLONGMETHOD);
	}

	//A space signifies the end of the method
	if (*buffer == ' ') {
		DEBUG("[#%d] Found first space seperator (mon), len: %d\n", connection->client_sock, n);

		//As long as the method is valid the next step
		//is to parse the url
		CONNECTION_HANDLER(connection, http_mon_handle_url);
		connection->state = 0;

		//Workout what valid method we have been given (if any)
		if (n == 3 && rbuf_cmpn(&connection->input, "GET", 3) == 0) {
			//This is a GET request
			connection->type = REQUEST_HTTPGET;
			assert(REQUEST_IS(connection->type, connection->type));
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP GET Request\n", connection->client_sock);
			return needs_more;
		}
		else if (n == 4 && rbuf_cmpn(&connection->input, "HEAD", 4) == 0) {
			//This is a HEAD request
			connection->type = REQUEST_HTTPHEAD;
			assert(REQUEST_IS(connection->type, connection->type));
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP HEAD Request\n", connection->client_sock);
			return needs_more;
		}

		//Else: This is an INVALID request
		RBUF_READMOVE(connection->input, n + 1);
		return http_write_response_after_eol(connection, HTTPTEMPLATE_FULLUNKNOWNMETHOD);
	}

	return continue_processing;
}

static inline state_action http_mon_read_requeststarturl(scache_connection* connection, char* buffer, int n) {
	//Assert: first char is a / (start of URL)
	assert(n != 0 || *buffer == '/');

	// search either for / (goto http_read_requeststarturl2) or " " goto next step
	if (n != 0) {
		//Skip unless character has meaning
		if (*buffer != ' ') {
			return continue_processing;//continue
		}
		printf("%c\n", *buffer);


		RBUF_READMOVE(connection->input, 1); 
		char* key = (char*)malloc(sizeof(char) * (n + 1));
		rbuf_copyn(&connection->input, key, n - 1);
		key[n - 1] = 0;//Null terminate the key
		RBUF_READMOVE(connection->input, n + 1);

		if(n == 1){ // URL: "/"
			free(key);
			if (REQUEST_IS(connection->type, REQUEST_HTTPHEAD)) {
				return http_headers_response_after_eol(connection, HTTPTEMPLATE_HEAD_ONLY);
			}else{
				return http_headers_response_after_eol(connection, HTTPTEMPLATE_FULL200OK);
			}
		} else if(n == 5 && strcmp(key, "conn") == 0){ // URL: "/conn"
			free(key);
			connection->state = 2;	
			CONNECTION_HANDLER(connection,  http_handle_mon_eolwrite_initial);
			return http_handle_mon_eolwrite_initial(connection);
		}

		DEBUG("[#%d] HTTP 404 Not Found for URL \"%s\" of length %d\n", connection->client_sock, key, n);
		free(key);
		return http_write_response_after_eol(connection, HTTPTEMPLATE_FULL404_PATHNOTFOUND);
	}

	return continue_processing;
}

state_action http_mon_handle_url(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	DEBUG("[#%d] Handling HTTP url for mon (Stage State: %d)\n", connection->client_sock, connection->state);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_mon_read_requeststarturl(connection, buffer, n));
	return ret;
}


state_action http_mon_handle_method(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;

	DEBUG("[#%d] Handling HTTP method\n", connection->client_sock);

	//Skip newlines at begining of request (bad clients)
	skip_over_newlines(&connection->input);

	connection->type = 0;

	//Process request line
	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_requeststartmethod_mon(connection, buffer, n));
	return ret;
}


state_action http_mon_handle_start(scache_connection* connection) {
	DEBUG("[#%d] Handling new HTTP connection\n", connection->client_sock);
	CONNECTION_HANDLER(connection,  http_mon_handle_method);
	return needs_more;
}

static scache_connection* mon_head = NULL; // next -> tail, prev = null
static scache_connection* mon_tail = NULL; // prev -> head, next = null

void monitoring_add(scache_connection* conn){
	memset(&conn->monitoring, 0, sizeof(conn->monitoring));
	assert(conn->monitoring.next == NULL);
	assert(conn->monitoring.prev == NULL);

	memcpy(&conn->monitoring.scheduled, (void*)&current_time, sizeof(current_time));

	// Add connection to the monitoring linked list
	if(mon_tail == NULL){
		mon_tail = mon_head = conn;
	}else{
		// insert at tail
		assert(mon_tail->monitoring.next == NULL);
		mon_tail->monitoring.next = conn;
		conn->monitoring.prev = mon_tail;
		mon_tail = conn;
	}
}

void monitoring_destroy(scache_connection* connection){
	assert(connection->ltype == mon_listener);
	scache_connection* t;

	/*
	head ... | connection | t | ... tail
	*/
	t = connection->monitoring.next;
	if(t != NULL){
		assert(t->monitoring.prev == connection);
		t->monitoring.prev = connection->monitoring.prev;
		if(t->monitoring.prev == NULL) {
			assert(mon_head == connection);
			mon_head = t;
		}
	}else{
		// Only if we are destroying the tail t will be NULL
		assert(connection == mon_tail);
		mon_tail = t;
	}

	/*
	head ... | t | connection | ... tail
	*/
	t = connection->monitoring.prev;
	if(t != NULL){
		assert(t->monitoring.next == connection);
		t->monitoring.next = connection->monitoring.next;
		if(t->monitoring.next == NULL) {
			assert(mon_tail == connection);
			mon_tail = t;
		}
	}else{
		// Only if we are destroying the head will t be null
		assert(mon_head == connection);
		mon_head = t;
	}
}

static void reverse(char s[])
{
	int i, j;
	char c;

	for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
}  

static void itoa(int n, char s[])
{
     int i, sign;

     if ((sign = n) < 0)  /* record sign */
         n = -n;          /* make n positive */
     i = 0;
     do {       /* generate digits in reverse order */
         s[i++] = n % 10 + '0';   /* get next digit */
     } while ((n /= 10) > 0);     /* delete it */
     if (sign < 0)
         s[i++] = '-';
     s[i] = '\0';
     reverse(s);
}

void monitoring_check(){
	scache_connection* conn;

	// Every 5ms we will work down any nodes in mon_head that need to be notified
	while(mon_head != NULL){
		conn = mon_head;
		assert(conn->monitoring.prev == NULL);

		// Not yet time
		if(timercmp(&conn->monitoring.scheduled, &current_time, >)) break;
		
		// move on
		mon_head = conn->monitoring.next;
		if(mon_head != NULL) mon_head->monitoring.prev = NULL;

		if(mon_tail != conn){
			mon_tail->monitoring.next = conn;
			conn->monitoring.prev = mon_tail;
			mon_tail = conn;
		}else{
			mon_tail = mon_head = conn;
			conn->monitoring.next = conn->monitoring.prev = NULL;
		}
		conn->monitoring.next = NULL;

		// If buffer wasnt cleared already, then we will need to disconnect
		if(conn->output_buffer != NULL){
			http_cleanup(conn);
			connection_remove(conn->client_sock);
			return;
		}

		// Add to output buffer
		// this will override anything already there
		// if we can't write in the time allocated just discard. It's not worth buffering.
		conn->output_buffer = monitoring_strings[conn->monitoring.current];
		conn->output_length = monitoring_lens[conn->monitoring.current];
		conn->monitoring.current ++;
		memcpy(&conn->monitoring.scheduled, (void*)&current_time, sizeof(current_time));
		conn->monitoring.scheduled.tv_sec += MONITORING_DEFAULT_INTERVAL;

		// signal for write registration
		connection_register_write(conn->client_sock);
	}
}

void monitoring_init(){
	puts("Monitoring init");
	// Pre-allocate monitoring string values up to a uint16 
	for(uint32_t i=0; i<=0xffff; i++){
		char* target = monitoring_strings[i];
		itoa(i, target);
		uint8_t len = (uint8_t)strlen(target);
		target[len] = '\n';
		target[len + 1] = 0;
		monitoring_lens[i] = len + 1;
	}
}