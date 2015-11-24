#include <string.h>
#include <stdio.h>
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include "connection.h"
#include "debug.h"
#include "http.h"
#include "settings.h"
#ifdef DEBUG_BUILD
/* For reference counting checks */
#include "db.h"
#endif

/* Globals */
cache_listeners listeners = { .fds = NULL, .fd_count = 0 };
struct epoll_event ev;
struct cache_connection_node ctable[CONNECTION_HASH_ENTRIES] = { 0 };

//Misc
volatile sig_atomic_t stop_soon = 0;

/* Methods */
static bool connection_event_update(int epfd, int fd, uint32_t events){
	assert(fd != 0 || fd);
	ev.events = events;
	ev.data.fd = fd;
	int res = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	if (res != 0){
		DEBUG("[#] epoll_ctl() update failed on fd: %d.\n", fd);
	}
	return res == 0;
}

bool connection_register_write(int epfd, int fd){
	return connection_event_update(epfd, fd, EPOLLOUT | EPOLLHUP);
}

bool connection_register_read(int epfd, int fd){
	return connection_event_update(epfd, fd, EPOLLIN | EPOLLHUP | EPOLLRDHUP);
}

void connection_setup(struct scache_bind* binds, int num_binds) {
	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++){
		ctable[i].connection.client_sock = -1;
	}
	
	listeners.fd_count = num_binds;
	listeners.fds = (int*)malloc(sizeof(int) * num_binds);
	for (int i = 0; i < num_binds; i++)
	{
		listeners.fds[i] = connection_open_listener(binds[i]);
	}
	
}

/* Portable function to set a socket into nonblocking mode.
Calling this on a socket causes all future read() and write() calls on
that socket to do only as much as they can immediately, and return
without waiting.
If no data can be read or written, they return -1 and set errno
to EAGAIN (or EWOULDBLOCK).
Thanks to Bjorn Reese for this code. */
int connection_non_blocking(int fd)
{
	int flags;

	/* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
	/* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
	/* Otherwise, use the old way of doing it */
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}


void connection_close_listener(){
	for (int i = 0; i < listeners.fd_count; i++)
	{
		close(listeners.fds[i]);
		listeners.fds[i] = -1;	
	}
}

int connection_open_listener(struct scache_bind ibind) {
	int res;
	int listenfd;
	/* Set up to be a daemon listening on port 8000 */
	listenfd = socket(ibind.af, SOCK_STREAM, 0);


	/* Enable address reuse */
	int on = 1;
	res = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (res < 0){
		goto fail;
	}

	//bind1
	sockaddr* tobind;
	int tobind_len;
	switch (ibind.af)
	{
	case AF_INET:
		struct sockaddr_in servaddr;
		tobind = (sockaddr*)&servaddr;
		tobind_len = sizeof(servaddr);
		memset(&servaddr, 0, sizeof(servaddr));
		servaddr.sin_family = ibind.af;
		memcpy(&servaddr.sin_addr.s_addr, &ibind.addr, sizeof(servaddr.sin_addr.s_addr));
		servaddr.sin_port = htons(ibind.port);
		
	case AF_INET6:
		struct sockaddr_in6 servaddr6;
		tobind = (sockaddr*)&servaddr6;
		tobind_len = sizeof(servaddr6);
		memset(&servaddr6, 0, sizeof(servaddr6));
		servaddr6.sin6_family = ibind.af;
		memcpy(&servaddr6.sin6_addr.__in6_u, &ibind.addr, sizeof(servaddr6.sin6_addr.__in6_u));
		servaddr6.sin6_port = htons(ibind.port);
		
	case AF_UNIX:
		struct sockaddr_un unaddr;
		tobind = (sockaddr*)&unaddr;
		tobind_len = sizeof(unaddr);
		memset(&unaddr, 0, sizeof(unaddr));
		unaddr.sun_family = ibind.af;
		memcpy(&unaddr.sun_path, &ibind.addr, sizeof(unaddr.sun_path));
	}
	
	res = bind(listenfd, tobind, tobind_len);
	if (res < 0){
		goto fail;
	}
	/* Force the network socket into nonblocking mode */
	res = connection_non_blocking(listenfd);
	if (res < 0){
		goto fail;
	}


	res = listen(listenfd, 10);
	if (res < 0){
		goto fail;
	}

	SAYF("Listening on %d\n", ibind.port);

	return listenfd;
fail:
	PFATAL("error opening listener (:%d)", ibind.port);
}

static cache_connection* connection_add(int fd, cache_connection_node* ctable){
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock != -1){
		while (node->next != NULL) {
			assert(node->connection.client_sock != -1);
			node = node->next;
		}

		cache_connection_node* newNode = (cache_connection_node*)malloc(sizeof(cache_connection_node));
		node->next = newNode;
		newNode->next = NULL;
		node = newNode;
	}
	else{
		node->next = NULL;
	}

	rbuf_init(&node->connection.input);
	node->connection.state = 0;
	node->connection.client_sock = fd;
	node->connection.output_buffer_free = NULL;
	node->connection.writing = false;

	return &node->connection;
}

static cache_connection* connection_get(int fd, cache_connection_node* ctable){
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock == -1){
		return NULL;
	}
	while (node->connection.client_sock != fd){
		assert(node->connection.client_sock != -1);
		node = node->next;
		if (node == NULL){
			return NULL;
		}
	}

	return &node->connection;
}

static void connection_remove(int epfd, int fd, cache_connection_node* ctable){
	cache_connection_node* prev = NULL;
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock == -1){
		WARN("Unable to find fd: %d connection entry to remove", fd);
		return;
	}
	while (node->connection.client_sock != fd){
		assert(node->connection.client_sock != -1);
		prev = node;
		node = node->next;
		if (node == NULL){
			WARN("Unable to find fd: %d connection entry to remove, reached end of list", fd);
			return;
		}
	}

	if (prev){
		//Not the first node in a linked list
		prev->next = node->next;
		free(node);
	}
	else{
		if (node->next){
			//Has nodes after it, but is the first node
			memcpy(&node->connection, &node->next->connection, sizeof(cache_connection));

			//Set node->next to node->next->next then free next->next (temp var: prev)
			prev = node->next;
			node->next = prev->next;
			free(prev);
		}
		else{
			//Is a single entry in table
			node->connection.client_sock = -1;
		}
	}
}

static int connection_count(cache_connection_node* ctable){
	int count = 0;
	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++){
		cache_connection_node* target = &ctable[i];
		do {
			if (target->connection.client_sock == -1){
				break;
			}
			count++;
			target = target->next;
		} while (target != NULL);
	}
	return count;
}

static bool is_listener(int fd)
{
	
	for (int i = 0; i < listeners.fd_count; i++)
	{
		if (listeners.fds[i] == fd)
		{
			return true;
		}
	}
	return false;
}

void connection_event_loop(void (*connection_handler)(cache_connection* connection)){
	int epfd = epoll_create(MAXCLIENTS);
	struct epoll_event events[NUM_EVENTS];
	int max_listener = 0;
	int res;
	
	for (int i = 0; i < listeners.fd_count; i++)
	{
		ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
		ev.data.fd = listeners.fds[i];
		if (max_listener < listeners.fds[i])
		{
			max_listener = listeners.fds[i];
		}
		res = epoll_ctl(epfd, EPOLL_CTL_ADD, listeners.fds[i], &ev);
		if (res != 0) {
			PFATAL("epoll_ctl() failed.");
		}
	}

	while (!stop_soon) {
		int nfds = epoll_wait(epfd, events, NUM_EVENTS, 500);
		int n = 0;
		while (n < nfds) {
			int fd = events[n].data.fd;
			if (fd <= max_listener && is_listener(fd)) {
				if (events[n].events & EPOLLIN){
					DEBUG("[#] Accepting connection\n");
					int client_sock = accept(fd, NULL, NULL);

					if (client_sock < 0) {
						WARN("Unable to handle API connection: accept() fails. Error: %s", strerror(errno));
					}
					else {
						DEBUG("[#] Accepted connection %d\n", client_sock);

						//Connection will be non-blocking
						if (connection_non_blocking(client_sock) < 0)
							PFATAL("Setting connection to non blocking failed.");

						//Enable TCP CORK
						int state = 1;
						setsockopt(client_sock, IPPROTO_TCP, TCP_CORK, &state, sizeof(state));

						//Add socket to epoll
						ev.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP;
						ev.data.fd = client_sock;
						res = epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &ev);
						if (res != 0){
							PFATAL("epoll_ctl() failed.");
						}

						//Store connection
						cache_connection* connection = connection_add(client_sock, ctable);

						//Handle event
						connection_handler(connection);
					}
				}
				else if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP){
					FATAL("listener socket is down.");
				}
			}
			else{
				DEBUG("[#%d] Got socket event %d\n", fd, events[n].events);
				cache_connection* connection = connection_get(fd, ctable);
				if (connection != NULL){
					int close_connection = 0;

					if (events[n].events & EPOLLIN){
						if (http_read_handle(epfd, connection) == close_connection){
							close_connection = 1;
						}
					}
					else if (events[n].events & EPOLLOUT){
						if (http_write_handle(epfd, connection) == close_connection){
							close_connection = 1;
						}
					}
					else if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP || events[n].events & EPOLLRDHUP){
						close_connection = 1;
					}


					if (close_connection){
						DEBUG("[#%d] Closing connection\n", fd);
						http_cleanup(connection);
						assert(fd != 0 || settings.daemon_mode);
						close(fd);
						connection_remove(epfd, fd, ctable);
						assert(connection_get(fd, ctable) == NULL);
#ifdef DEBUG_BUILD
						int num_connections = connection_count(ctable);
						if (num_connections == 0){
							db_check_table_refs();
						}
#endif
					}
				}
				else{
					WARN("Unknown connection %d", fd);
					assert(fd != 0 || settings.daemon_mode);
					close(fd);
				}
			}
			n++;
		}
	}

	close(epfd);
}

/*
On close connection cleanup routine
*/
void connection_cleanup_http(cache_connection_node* connection, bool toFree = false){
	assert(connection != NULL);

	//Close socket to client
	if (connection->connection.client_sock != -1){
		http_cleanup(&connection->connection);
		close(connection->connection.client_sock);
		connection->connection.client_sock = -1;
	}

	//Handle chained connections
	if (connection->next != NULL){
		connection_cleanup_http(connection->next, true);
	}

	//Free up the connection if dynamically allocated
	if (toFree){
		free(connection);
	}
}

void connection_cleanup(){
	if (listeners.fds != NULL){
		connection_close_listener();
	}

	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++){
		connection_cleanup_http(&ctable[i]);
	}
}