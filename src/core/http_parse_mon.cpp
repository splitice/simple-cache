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
#include <netinet/tcp.h>
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
static char hostname[1024];
static uint16_t hostname_len;
static char* monitoring_rsp = NULL;
static int monitoring_rsp_len = 0;

static void enable_keepalive(int sock) {
    int yes = 1;
	setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int));

    int idle = MONITORING_DEFAULT_INTERVAL + 1;
	setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(int));

    int interval = 3;
	setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(int));

    int maxpkt = 2;
	setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof(int));
}


static state_action http_write_response_after_eol(scache_connection* connection, int http_template) {
	CONNECTION_HANDLER(connection, http_handle_eolwritetoend);
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	connection->state = 0;
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

state_action http_respond_writecount_starting(scache_connection* connection) {
	DEBUG("[#%d] Starting write count\n", connection->client_sock);
	//Static response, after witing, read next request
	CONNECTION_HANDLER(connection,  http_mon_handle_method);
	return continue_processing;
}

static state_action http_respond_start_to_count(scache_connection* connection) {
	DEBUG("[#%d] Ready to start to count \n", connection->client_sock);
	monitoring_add(connection);
	enable_keepalive(connection->client_sock);
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

		// build the response
		connection->output_buffer = monitoring_rsp;
		connection->output_length = monitoring_rsp_len;

		CONNECTION_HANDLER(connection,  http_respond_start_to_count);
		bool res = connection_register_write(connection);

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
	if (n != 0 && ret == needs_more_read) {
		RBUF_READMOVE(connection->input, n);
	}

	return ret;
}




static state_action http_headers_response_count(scache_connection* connection, int http_template) {
	connection->state = 2;
	CONNECTION_HANDLER(connection,  http_handle_mon_eolwrite_initial);
	connection->output_buffer = http_templates[http_template];
	connection->output_length = http_templates_length[http_template];
	return needs_more_read;
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
			connection->method = REQUEST_HTTPGET;
			assert(REQUEST_IS(connection->method, connection->method));
			RBUF_READMOVE(connection->input, n + 1);
			DEBUG("[#%d] HTTP GET Request\n", connection->client_sock);
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

		RBUF_READMOVE(connection->input, 1); 
		char* key = (char*)malloc(sizeof(char) * (n + 1));
		rbuf_copyn(&connection->input, key, n - 1);
		key[n - 1] = 0;//Null terminate the key
		RBUF_READMOVE(connection->input, n + 1);

		if(n == 1){ // URL: "/"
			free(key);
			if (REQUEST_IS(connection->method, REQUEST_HTTPHEAD)) {
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
	assert(!connection->epollout && connection->epollin);

	DEBUG("[#%d] Handling HTTP url for mon (Stage State: %d)\n", connection->client_sock, connection->state);

	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_mon_read_requeststarturl(connection, buffer, n));
	return ret;
}


state_action http_mon_handle_method(scache_connection* connection) {
	char* buffer;
	int end, n;
	state_action ret = continue_processing;
	assert(!connection->epollout && connection->epollin);


	DEBUG("[#%d] Handling HTTP method\n", connection->client_sock);

	//Skip newlines at begining of request (bad clients)
	skip_over_newlines(&connection->input);

	connection->method = 0;

	//Process request line
	RBUF_ITERATE(connection->input, n, buffer, end, ret, http_read_requeststartmethod_mon(connection, buffer, n));
	return ret;
}


state_action http_mon_handle_start(scache_connection* connection) {
	DEBUG("[#%d] Handling new HTTP connection\n", connection->client_sock);
	CONNECTION_HANDLER(connection,  http_mon_handle_method);
	return needs_more_read;
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
		assert(mon_head == NULL);
		mon_tail = mon_head = conn;
	}else{
		// insert at tail
		assert(mon_head->monitoring.prev == NULL);
		mon_head->monitoring.prev = conn;
		conn->monitoring.next = mon_head;
		mon_head = conn;
	}
}

void monitoring_destroy(scache_connection* connection){
	assert(connection->ltype == mon_connection);
	scache_connection* t;

	if(mon_head == connection){
		assert(connection->monitoring.prev == NULL);
		mon_head = connection->monitoring.next;
	}

	if(mon_tail == connection){
		assert(connection->monitoring.next == NULL);
		mon_tail = connection->monitoring.prev;
		if(mon_head == NULL){
			assert(mon_tail == NULL);
			// early exit we remvoed from both ends
			return;
		}
	}

		
	/*
	head ... | connection | t | ... tail
	*/
	t = connection->monitoring.next;
	if(t != NULL){
		assert(t->monitoring.prev == connection);
		t->monitoring.prev = connection->monitoring.prev;
	}

	/*
	head ... | t | connection | ... tail
	*/
	t = connection->monitoring.prev;
	if(t != NULL){
		assert(t->monitoring.next == connection);
		t->monitoring.next = connection->monitoring.next;
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

bool monitoing_needs_to_run(){
	if(mon_head == NULL) return false;
	if(timercmp(&mon_head->monitoring.scheduled, &current_time, >)) return false;
	return true;
}

void monitoring_check(){
	scache_connection* conn;
	int fd;
	timeval current_time_copy = *(timeval*)&current_time;

	// Every 5ms we will work down any nodes in mon_head that need to be notified
	while(mon_head != NULL){
		conn = mon_head;
		assert(conn->monitoring.prev == NULL);

		// Not yet time
		if(timercmp(&conn->monitoring.scheduled, &current_time_copy, >)) break;
		
		// move on
		mon_head = conn->monitoring.next;
		assert(conn != mon_head);
		if(mon_head != NULL) mon_head->monitoring.prev = NULL;

		// If buffer wasnt cleared already, then we will need to disconnect
		if(conn->output_buffer != NULL){
			fd = conn->client_sock;
			DEBUG("Failed to output buffer to monitoring connection, will close fd %d\n", fd);
			http_cleanup(conn);
			connection_remove(conn);
			close_socket(fd);
			continue;
		}

		// Move to end (tail)
		if(mon_tail != conn){
			mon_tail->monitoring.next = conn;
			conn->monitoring.prev = mon_tail;
			mon_tail = conn;
		}else{
			mon_tail = mon_head = conn;
			conn->monitoring.prev = NULL;
		}
		conn->monitoring.next = NULL;

		// Add to output buffer
		// this will override anything already there
		// if we can't write in the time allocated just discard. It's not worth buffering.
		conn->output_buffer = monitoring_strings[conn->monitoring.current];
		conn->output_length = monitoring_lens[conn->monitoring.current];
		conn->monitoring.current ++;
	
		conn->monitoring.scheduled.tv_usec = current_time_copy.tv_usec;
		conn->monitoring.scheduled.tv_sec = current_time_copy.tv_sec + MONITORING_DEFAULT_INTERVAL;


		// signal for write registration
		connection_register_write(conn);
	}

	#ifdef DEBUG_MONITORING
	conn = mon_head;
	while(conn != NULL){
		if(conn->monitoring.next != NULL){
			assert(!timercmp(&conn->monitoring.scheduled, &conn->monitoring.next->monitoring.scheduled, >));
		}
		conn = conn->monitoring.next;
	}
	#endif
}

void monitoring_init(){
	// get the hostname
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	hostname_len = strlen(hostname);

	// build the response
	monitoring_rsp_len = http_templates_length[HTTPTEMPLATE_MON_STREAM] + hostname_len - 2;
	monitoring_rsp = (char*)malloc(monitoring_rsp_len + 1);
	sprintf(monitoring_rsp, http_templates[HTTPTEMPLATE_MON_STREAM], hostname);


	printf("Monitoring init on %s\n", hostname);
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

void monitoring_close(){
	scache_connection* conn;
	int flag = 1; 
	static scache_connection* close_head = mon_head;

	while(close_head != NULL){
		conn = close_head;
		
		// move on
		close_head = conn->monitoring.next;

		// hard write before close
		write(conn->client_sock, "q\n", 2);
		shutdown(conn->client_sock, SHUT_WR);
	}
}