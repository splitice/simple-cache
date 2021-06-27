#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <fcntl.h>
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
#include <sys/eventfd.h>
#include <pthread.h>
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
listener_collection scache_listeners = { .listeners = NULL, .listener_count = 0 };
struct epoll_event ev;
struct scache_connection_node ctable[CONNECTION_HASH_ENTRIES] = { 0 };

int epfd;
volatile sig_atomic_t stop_soon = 0;

bool connection_stop_soon(){
	return stop_soon != 0;
}

struct connection_thread_arg
{
	int eventfd;
	listener_type type;
};

struct connections_queued
{
	int client_sock;
	listener_type client_type;
	connections_queued* next;
};

static volatile connections_queued* cq_head = NULL;
static volatile connections_queued* cq_tail = NULL;
static pthread_mutex_t cq_lock;

/* Methods */
static bool connection_event_update(int fd, uint32_t events) {
	assert(fd != 0 || fd);
	ev.events = events;
	ev.data.fd = fd;
	int res = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	if (res != 0) {
		DEBUG("[#] epoll_ctl() update failed on fd: %d.\n", fd);
	}
	return res == 0;
}

bool connection_register_write(int fd) {
	return connection_event_update(fd, EPOLLOUT | EPOLLHUP);
}

bool connection_register_read(int fd) {
	return connection_event_update(fd, EPOLLIN | EPOLLHUP | EPOLLRDHUP);
}

void connection_setup(struct scache_binds cache_binds, struct scache_binds monitor_binds) {
	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++) {
		ctable[i].connection.client_sock = -1;
	}

	// Allocate for all listeners
	scache_listeners.listener_count = cache_binds.num + monitor_binds.num;
	scache_listeners.listeners = (struct listener_entry*)malloc(sizeof(struct listener_entry) * scache_listeners.listener_count);
	
	// Caching
	for (int i = 0; i < cache_binds.num; i++)
	{
		scache_listeners.listeners[i].fd = connection_open_listener(cache_binds.binds[i]);
		scache_listeners.listeners[i].type = cache_listener;
	}
	
	// Monitoring
	for (int i = 0; i < monitor_binds.num; i++)
	{
		scache_listeners.listeners[i + cache_binds.num].fd = connection_open_listener(monitor_binds.binds[i]);
		scache_listeners.listeners[i + cache_binds.num].type = mon_listener;
	}
}

/* Portable function to set a socket into nonblocking mode.
Calling this on a socket causes all future read() and write() calls on
that socket to do only as much as they can immediately, and return
without waiting.
If no data can be read or written, they return -1 and set errno
to EAGAIN (or EWOULDBLOCK).
Thanks to Bjorn Reese for this code. */
static int connection_non_blocking(int fd)
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


void connection_close_listeners() {
	int fd;
	
	WARN("Closing %d listeners", scache_listeners.listener_count);

	for (uint32_t i = 0; i < scache_listeners.listener_count; i++)
	{
		fd = scache_listeners.listeners[i].fd;
		scache_listeners.listeners[i].fd = -1;	
		close(fd);
	}

	free(scache_listeners.listeners);
	scache_listeners.listeners = NULL;
}

static int connection_open_bind(struct scache_bind ibind, int listenfd)
{
	union {
		struct sockaddr_in servaddr;
		struct sockaddr_in6 servaddr6;
		struct sockaddr_un unaddr;
	} tobind;
	int tobind_len;
	int res;
	int enable = 1;
	
	switch (ibind.af)
	{
	case AF_INET:
		tobind_len = sizeof(tobind.servaddr);
		memset(&tobind.servaddr, 0, sizeof(tobind.servaddr));
		tobind.servaddr.sin_family = ibind.af;
		memcpy(&tobind.servaddr.sin_addr.s_addr, &ibind.addr, sizeof(tobind.servaddr.sin_addr.s_addr));
		tobind.servaddr.sin_port = htons(ibind.port);
		break;
		
	case AF_INET6:
		tobind_len = sizeof(tobind.servaddr6);
		memset(&tobind.servaddr6, 0, sizeof(tobind.servaddr6));
		tobind.servaddr6.sin6_family = ibind.af;
		memcpy(&tobind.servaddr6.sin6_addr.__in6_u, &ibind.addr, sizeof(tobind.servaddr6.sin6_addr.__in6_u));
		tobind.servaddr6.sin6_port = htons(ibind.port);
		break;
		
	case AF_UNIX:
		tobind_len = sizeof(tobind.unaddr);
		memset(&tobind.unaddr, 0, sizeof(tobind.unaddr));
		tobind.unaddr.sun_family = ibind.af;
		unlink(ibind.addr);
		memcpy(&tobind.unaddr.sun_path, &ibind.addr, sizeof(tobind.unaddr.sun_path));
		break;
		
	default:
		FATAL("Unknown address family, cant bind");
	}

	if(ibind.transparent){
		if(-1 == setsockopt(listenfd, SOL_IP, IP_TRANSPARENT, (const char*)&enable, sizeof(enable))){
			return -errno;
		}
	}
	
	res = bind(listenfd, (sockaddr*)&tobind, tobind_len);
	if (res < 0) {
		return -errno;
	}
	
	if (ibind.af == AF_UNIX)
	{
		res = chmod(tobind.unaddr.sun_path, 0777);
		if (res < 0) {
			return res;
		}
	}
	
	return 0;
}

int connection_open_listener(struct scache_bind ibind) {
	int res;
	int listenfd;
	int on = 1;
	
	/* Set up to be a daemon listening on port 8000 */
	listenfd = socket(ibind.af, SOCK_STREAM, 0);
	if (listenfd < 0) {
		goto fail;
	}

	/* Enable address reuse */
	if (ibind.af == AF_INET || ibind.af == AF_INET6)
	{
		res = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		if (res < 0) {
			goto fail;
		}
	}

	//bind
	res = connection_open_bind(ibind, listenfd);
	if (res < 0)
	{
		goto fail;
	}
	
	/* Force the network socket into nonblocking mode */
	res = connection_non_blocking(listenfd);
	if (res < 0) {
		goto fail;
	}

	res = listen(listenfd, 4096);
	if (res < 0) {
		goto fail;
	}

	SAYF("Listening on %d (fd %d)\n", ibind.port, listenfd);

	return listenfd;
fail:
	PFATAL("error opening listener (:%d)", ibind.port);
	return -1;
}

static scache_connection* connection_add(int fd, listener_type client_type) {
	scache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	scache_connection_node* newNode = NULL;

	if (node->connection.client_sock != -1) {
		while (node->next != NULL) {
			assert(node->connection.client_sock != -1);
			node = node->next;
		}

		newNode = (scache_connection_node*)malloc(sizeof(scache_connection_node));
	}else{
		newNode = node;
	}
	
	// Initialize connection
	memset(newNode, 0, sizeof(*newNode)); /* .connection = {}, .next = NULL */

	// safe to set first
	newNode->connection.ltype = client_type;
	if(client_type == cache_listener){
		newNode->connection.cache.target.key.fd = -1;
	}
	rbuf_init(&(newNode->connection.input));

	// do last as marks connection slot as used
	newNode->connection.client_sock = fd;

	// this is a chained connection
	if(node != newNode){
		node->next = newNode;
		node = newNode;
	}

	return &(node->connection);
}

static scache_connection* connection_get(int fd) {
	scache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock == -1) {
		return NULL;
	}
	assert(node->connection.client_sock >= 0);
	
	while (node->connection.client_sock != fd) {
		assert(node->connection.client_sock != -1);
		node = node->next;
		if (node == NULL) {
			return NULL;
		}
	}

	return &(node->connection);
}

bool connection_remove(int fd) {
	scache_connection_node* node;
	scache_connection_node* temp = NULL;
	assert(fd >= 0);
	node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock == -1) {
		WARN("Unable to find fd: %d connection entry to remove", fd);
		return false;
	}
	while (node->connection.client_sock != fd) {
		assert(node->connection.client_sock != -1);
		temp = node; /* prev */
		node = node->next;
		if (node == NULL) {
			WARN("Unable to find fd: %d connection entry to remove, reached end of list", fd);
			return false;
		}
	}

	// Clear current record
	node->connection.client_sock = -1;

	/* is in the middle */
	if (temp) { /* prev */
		//Not the first node in a linked list
		temp->next = node->next;
		free(node);
	}
	else if (temp = node->next) 
	{ 
		/* Has nodes after it, but is the first node */
		// Copy next node into current node connection (static memory)
		memcpy(&(node->connection), &(temp->connection), sizeof(scache_connection));

		//Set node->next to node->next->next then free next->next
		node->next = temp->next;
		free(temp);
	} //Else: Is a single entry in table and just setting to -1 will suffice

	return true;
}

static unsigned int connection_count() {
	unsigned count = 0;
	for (unsigned int i = 0; i < CONNECTION_HASH_ENTRIES; i++) {
		scache_connection_node* target = &ctable[i];
		do {
			if (target->connection.client_sock == -1) {
				break;
			}
			count++;
			target = target->next;
		} while (target != NULL);
	}

	return count;
}

static unsigned int connection_any() {\
	for (unsigned int i = 0; i < CONNECTION_HASH_ENTRIES; i++) {
		scache_connection_node* target = &ctable[i];
		if (target->connection.client_sock != -1) return true;
	}

	return false;
}

static void* connection_handle_accept(void *arg)
{
	int epacceptfd = epoll_create(scache_listeners.listener_count + 1);
	struct epoll_event events[NUM_EVENTS_ACCEPT];
	int res;
	connection_thread_arg* thread_arg = (connection_thread_arg*)arg;
	uint64_t u = 1;
	listener_type our_type = thread_arg->type;
	int eventfd = thread_arg->eventfd;
	int enable = 1;
	
	for (uint32_t i = 0; i < scache_listeners.listener_count; i++)
	{
		if(scache_listeners.listeners[i].type != our_type) continue;

		ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
		ev.data.fd = scache_listeners.listeners[i].fd;
		res = epoll_ctl(epacceptfd, EPOLL_CTL_ADD, scache_listeners.listeners[i].fd, &ev);
		if (res != 0) {
			PFATAL("connection_handle_accept epoll_ctl() failed.");
		}
	}
	
	while (!stop_soon) {
		int nfds = 0;
		do {
			nfds = epoll_wait(epacceptfd, events, NUM_EVENTS_ACCEPT, 500);
			if(nfds == -1){
				if(errno == EINTR){
					nfds = 0;
					continue;
				}
				PFATAL("connection_handle_accept epoll_wait() failed");
				goto end;
			}
		} while(nfds == 0 && !stop_soon);
		assert(nfds >= 0);
		int n = 0;
		int state = 1;
		while (n < nfds) {
			int fd = events[n].data.fd;
			if (events[n].events & (EPOLLERR | EPOLLHUP))
			{
				FATAL("listener socket is down.");
			}
			else if (events[n].events & EPOLLIN)
			{
				DEBUG("[#] Accepting connection from fd %d of type %s\n", fd, listener_type_string(our_type));
				int client_sock = accept(fd, NULL, NULL);

				if(-1 == setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&enable, sizeof(enable))){
					DEBUG("[#] Unable to set tcp nodelay\n");
				}

				if (client_sock < 0) {
					if (errno != EAGAIN && errno != EWOULDBLOCK)
					{
						WARN("[#] accept() failed on fd %d of type %s. Error: %s", fd, listener_type_string(our_type), strerror(errno));
					}
					n++;
					continue;
				}
				else {
					DEBUG("[#] Accepted connection %d on fd %d of type %s\n", client_sock, fd, listener_type_string(our_type));

					//Connection will be non-blocking
					if (connection_non_blocking(client_sock) < 0)
						PFATAL("Setting connection to non blocking failed on fd %d of type %s.", fd, listener_type_string(our_type));
					
					//Enable TCP CORK
					setsockopt(client_sock, IPPROTO_TCP, TCP_CORK, &state, sizeof(state));
					
					connections_queued* q = (connections_queued*)malloc(sizeof(connections_queued));
					if(q == NULL){
						close(client_sock);
						n++;
						WARN("[#] failed to allocate memory for connection. Abandoning incoming connection.");
						continue;
					}
					q->client_sock = client_sock;
					q->client_type = our_type;
					q->next = NULL;
					
					// Insert connection into queue
					pthread_mutex_lock(&cq_lock);
					if (cq_tail == NULL)
					{
						assert(cq_head == NULL);
						cq_tail = q;
						cq_head = q;
					}
					else
					{
						cq_tail->next = q;
						cq_tail = q;
					}
					pthread_mutex_unlock(&cq_lock);
					
					//Write a signal
					int res;
					do {
						res = write(eventfd, &u, sizeof(u));
						if(res == -1){
							PFATAL("Unable to write to eventfd");
						}
						assert(res == sizeof(u));
					} while(!res);
				}
				n++;
			}
		}
	}

end:
	close(epacceptfd);

	return NULL;
}

void connection_event_loop(void (*connection_handler)(scache_connection* connection)) {
	epfd = epoll_create(MAXCLIENTS);
	struct epoll_event events[NUM_EVENTS];
	int max_listener = 0;
	int res;
	int efd;
	pthread_t tid[2];
	uint64_t u;
	
	//Init Mutex
	if (pthread_mutex_init(&cq_lock, NULL) != 0)
	{
		PFATAL("mutex init failed");
	}
	
	//Init Acceptor thread
	connection_thread_arg thread_arg[2];
	efd = eventfd(0, 0);
	thread_arg[0].type = cache_listener;
	thread_arg[1].type = mon_listener;
	thread_arg[0].eventfd = thread_arg[1].eventfd = efd;
	res = pthread_create(&tid[0], NULL, &connection_handle_accept, (void*)thread_arg);
	if (res != 0)
		PFATAL("can't create cache accept thread");
	res = pthread_create(&tid[1], NULL, &connection_handle_accept, (void*)(thread_arg + 1));
	if (res != 0)
		PFATAL("can't create mon accept thread");
	
	//Add messaging socket
	ev.events = EPOLLIN;
	ev.data.fd = efd;
	res = epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &ev);

	while (!stop_soon) {
		// Tight epoll wait loop
		int nfds = 0;
		do {
			nfds = epoll_wait(epfd, events, NUM_EVENTS, 500);
			if(nfds == -1){
				if(errno == EINTR){
					nfds = 0;
					continue;
				}
				PFATAL("epoll_wait() failed");
			}
		} while(nfds == 0 && !stop_soon);


		// First accept connections
		for (int n = 0; n < nfds; n++) {
			int fd = events[n].data.fd;
			if (fd == efd)
			{				
				res = read(fd, &u, sizeof(u));
				
				while (!stop_soon)
				{					
					//Dequeue
					pthread_mutex_lock(&cq_lock);
					connections_queued* temp = (connections_queued*)cq_head;
					if(temp != NULL){
						cq_head = temp->next;
						if (cq_head == NULL)
						{
							cq_tail = NULL;
						}
					}
					pthread_mutex_unlock(&cq_lock);

					// We are done
					if(temp == NULL){
						break;
					}

					// Handle dequeued socket
					assert(temp != NULL);
					int client_sock = temp->client_sock;
					listener_type client_type = temp->client_type;
					free(temp);
					assert(client_sock >= 0);
					
					//Handle connection
					DEBUG("[#%d] A new %s socket was accepted %d\n", fd, listener_type_string(client_type), client_sock);
					scache_connection* connection = connection_add(client_sock, client_type);
					assert(connection->client_sock == client_sock);
					connection_handler(connection);
					
					//Add socket to epoll
					ev.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP;
					ev.data.fd = client_sock;
					res = epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &ev);
					if (res != 0) {
						PWARN("epoll_ctl() failed to add %d.", client_sock);
					}
				}
			}
		}

		// Then process existing connections
		for (int n = 0; n < nfds; n++) {
			int fd = events[n].data.fd;
			if (fd != efd)
			{
				DEBUG("[#%d] Got socket event %d (in=%d, out=%d, hup=%d)\n", fd, events[n].events, events[n].events & EPOLLIN ? 1 : 0, events[n].events & EPOLLOUT ? 1 : 0, events[n].events & EPOLLHUP ? 1 : 0);
				scache_connection* connection = connection_get(fd);
				if (connection != NULL) {
					assert(connection->client_sock == fd);
					bool do_close = events[n].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP);

					if (events[n].events & EPOLLIN) {
						if (http_read_handle(connection) == close_connection) {
							do_close = true;
						}
					}
					if (events[n].events & EPOLLOUT) {
						if (http_write_handle(connection) == close_connection) {
							do_close = true;
						}
					}
					
					if (do_close) {
						DEBUG("[#%d] Closing connection due to err:%d hup:%d rdhup:%d\n", fd, !! (events[n].events&EPOLLERR), !! (events[n].events&EPOLLHUP), !! (events[n].events&EPOLLRDHUP));
						http_cleanup(connection);
						assert(fd != 0 || (settings.daemon_mode && fd >= 0));
						if(connection_remove(fd)){
							assert(connection_get(fd) == NULL);
		#ifdef DEBUG_BUILD
							if (!connection_any()) {
								db_check_table_refs();
							}
		#endif
						} else {
							WARN("Unable to remove connection %d (not found in table)", fd);
						}

						//Close connection socket
						close(fd);
					}
				}
				else if(fd == efd)
				{
					WARN("Unknown connection %d (in=%d, out=%d, hup=%d)\n", fd, events[n].events & EPOLLIN ? 1 : 0, events[n].events & EPOLLOUT ? 1 : 0, events[n].events & EPOLLHUP ? 1 : 0);
					if(fd) assert(fd);
					assert(fd != 0 || (settings.daemon_mode && fd >= 0));
					
					// Ensure connection has been removed
					ev.events = 0;
					ev.data.fd = fd;
					res = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev);
					if (res != 0) {
						PFATAL("epoll_ctl() failed.");
					}
				}
				
			}
		}
	}

end:
	close(epfd);
	
	errno = pthread_join(tid[0], NULL);
	if(errno != 0) {
		PFATAL("failed to join cache thread");
	}
	errno = pthread_join(tid[1], NULL);
	if(errno != 0) {
		PFATAL("failed to join mon thread");
	}

	close(efd);
}

/*
On close connection cleanup routine
*/
void connection_cleanup_http(scache_connection_node* connection, bool toFree = false) {
	assert(connection != NULL);
	int fd;

	//Close socket to client
	if (connection->connection.client_sock != -1) {
		http_cleanup(&connection->connection);
		fd = connection->connection.client_sock;
		connection->connection.client_sock = -1;
		close(fd);
	}

	//Handle chained connections
	if (connection->next != NULL) {
		connection_cleanup_http(connection->next, true);
	}

	//Free up the connection if dynamically allocated
	if (toFree) {
		free(connection);
	}
}

void connection_cleanup() {
	DEBUG("Performing cleanup\n");

	connections_queued* temp;
	if (scache_listeners.listeners != NULL) {
		connection_close_listeners();
	}

	// free active connections
	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++) {
		connection_cleanup_http(&ctable[i]);
	}

	// free queued connections
	while(cq_head != NULL){
		temp = (connections_queued*)cq_head;
		cq_head = cq_head->next;
		free(temp);
	}
}