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
cache_listeners listeners = { .fds = NULL, .fd_count = 0 };
struct epoll_event ev;
struct cache_connection_node ctable[CONNECTION_HASH_ENTRIES] = { 0 };

volatile sig_atomic_t stop_soon = 0;


struct connection_thread_arg
{
	int eventfd;
};

struct connections_queued
{
	int client_sock;
	connections_queued* next;
};

static connections_queued* cq_head = NULL;
static connections_queued* cq_tail = NULL;
static pthread_mutex_t cq_lock;

/* Methods */
static bool connection_event_update(int epfd, int fd, uint32_t events) {
	assert(fd != 0 || fd);
	ev.events = events;
	ev.data.fd = fd;
	int res = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	if (res != 0) {
		DEBUG("[#] epoll_ctl() update failed on fd: %d.\n", fd);
	}
	return res == 0;
}

bool connection_register_write(int epfd, int fd) {
	return connection_event_update(epfd, fd, EPOLLOUT | EPOLLHUP);
}

bool connection_register_read(int epfd, int fd) {
	return connection_event_update(epfd, fd, EPOLLIN | EPOLLHUP | EPOLLRDHUP);
}

void connection_setup(struct scache_bind* binds, uint32_t num_binds) {
	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++) {
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


void connection_close_listener() {
	for (uint32_t i = 0; i < listeners.fd_count; i++)
	{
		close(listeners.fds[i]);
		listeners.fds[i] = -1;	
	}
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
	
	res = bind(listenfd, (sockaddr*)&tobind, tobind_len);
	if (res < 0) {
		return res;
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

	SAYF("Listening on %d\n", ibind.port);

	return listenfd;
fail:
	PFATAL("error opening listener (:%d)", ibind.port);
}

static cache_connection* connection_add(int fd, cache_connection_node* ctable) {
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock != -1) {
		while (node->next != NULL) {
			assert(node->connection.client_sock != -1);
			node = node->next;
		}

		cache_connection_node* newNode = (cache_connection_node*)malloc(sizeof(cache_connection_node));
		node->next = newNode;
		node = newNode;
	}
	
	// Initialize connection
	memset(node, 0, sizeof(node)); /* .connection = {}, .next = NULL */
	rbuf_init(&node->connection.input);
	node->connection.client_sock = fd;

	return &node->connection;
}

static cache_connection* connection_get(int fd, cache_connection_node* ctable) {
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock == -1) {
		return NULL;
	}
	
	while (node->connection.client_sock != fd) {
		assert(node->connection.client_sock != -1);
		node = node->next;
		if (node == NULL) {
			return NULL;
		}
	}

	return &node->connection;
}

static bool connection_remove(int epfd, int fd, cache_connection_node* ctable) {
	cache_connection_node* temp = NULL;
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
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

	node->connection.client_sock = -1;
	if (temp) { /* prev */
		//Not the first node in a linked list
		temp->next = node->next;
		free(node);
	}
	else if (node->next) { /* Has nodes after it, but is the first node */
		// Copy next node into current node connection (static memory)
		memcpy(&node->connection, &node->next->connection, sizeof(cache_connection));

		//Set node->next to node->next->next then free next->next
		temp = node->next;
		node->next = temp->next;
		free(temp);
	} //Else: Is a single entry in table and setting to -1 will suffice

	return true;
}

static int connection_count(cache_connection_node* ctable) {
	int count = 0;
	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++) {
		cache_connection_node* target = &ctable[i];
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

static bool is_listener(int fd)
{
	
	for (uint32_t i = 0; i < listeners.fd_count; i++)
	{
		if (listeners.fds[i] == fd)
		{
			return true;
		}
	}
	return false;
}

static void* connection_handle_accept(void *arg)
{
	int epfd = epoll_create(MAXCLIENTS);
	struct epoll_event events[NUM_EVENTS];
	int res;
	connection_thread_arg* thread_arg = (connection_thread_arg*)arg;
	uint64_t u = 1;
	
	for (uint32_t i = 0; i < listeners.fd_count; i++)
	{
		ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
		ev.data.fd = listeners.fds[i];
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
			if (events[n].events & EPOLLIN) {
				DEBUG("[#] Accepting connection\n");
				int client_sock;
					
				do
				{
					client_sock = accept(fd, NULL, NULL);

					if (client_sock < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK)
						{
							break;
						}
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
							
						
						connections_queued* q = (connections_queued*)malloc(sizeof(connections_queued)) ;
						q->client_sock = client_sock;
						q->next = NULL;
						
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
						
						write(thread_arg->eventfd, &u, sizeof(uint64_t));
					}
				} while (!stop_soon);
			} else if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP) {
				FATAL("listener socket is down.");
			}
			n++;
		}
	}
	
	close(epfd);
}

void connection_event_loop(void (*connection_handler)(cache_connection* connection)) {
	int epfd = epoll_create(MAXCLIENTS);
	struct epoll_event events[NUM_EVENTS];
	int max_listener = 0;
	int res;
	int efd;
	pthread_t tid;
	uint64_t u;
	
	//Init Mutex
	if (pthread_mutex_init(&cq_lock, NULL) != 0)
	{
		PFATAL("mutex init failed");
	}
	
	//Init Acceptor thread
	connection_thread_arg* thread_arg = (connection_thread_arg*)malloc(sizeof(connection_thread_arg)) ;
	efd = eventfd(0, 0);
	thread_arg->eventfd = efd;
	res = pthread_create(&tid, NULL, &connection_handle_accept, (void*)thread_arg);
	if (res != 0)
		PFATAL("can't create accept thread");
	
	//Add messaging socket
	ev.events = EPOLLIN;
	ev.data.fd = efd;
	res = epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &ev);

	while (!stop_soon) {
		int nfds = epoll_wait(epfd, events, NUM_EVENTS, 500);
		int n = 0;
		while (n < nfds) {
			int fd = events[n].data.fd;
			if (fd == efd)
			{				
				res = read(fd, &u, sizeof(uint64_t));
				if (res != sizeof(uint64_t))
				{
					PFATAL("efd read() failed.");
				}
				
				while (u -- != 0)
				{
					assert(cq_head != NULL);
					int client_sock = cq_head->client_sock;
					assert(client_sock >= 0);
					DEBUG("[#%d] A new socket was accepted %d\n", fd, fd);
					
					//Dequeue
					assert(cq_head != NULL);
					connections_queued* temp = cq_head;
					pthread_mutex_lock(&cq_lock);
					cq_head = cq_head->next;
					if (cq_head == NULL)
					{
						cq_tail = NULL;
					}
					pthread_mutex_unlock(&cq_lock);
					free(temp);
					
					//Handle connection
					cache_connection* connection = connection_add(client_sock, ctable);
					connection_handler(connection);
					
					//Add socket to epoll
					ev.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP;
					ev.data.fd = connection->client_sock;
					res = epoll_ctl(epfd, EPOLL_CTL_ADD, connection->client_sock, &ev);
					if (res != 0) {
						PFATAL("epoll_ctl() failed.");
					}
				}
			}
			else
			{
				DEBUG("[#%d] Got socket event %d\n", fd, events[n].events);
				cache_connection* connection = connection_get(fd, ctable);
				if (connection != NULL) {
					bool do_close = false;

					if (events[n].events & EPOLLIN) {
						if (http_read_handle(epfd, connection) == close_connection) {
							do_close = true;
						}
					}
					if (events[n].events & EPOLLOUT) {
						if (http_write_handle(epfd, connection) == close_connection) {
							do_close = true;
						}
					}
					if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP || events[n].events & EPOLLRDHUP) {
						do_close = true;
					}
					
					if (do_close) {
						DEBUG("[#%d] Closing connection\n", fd);
						http_cleanup(connection);
						assert(fd != 0 || settings.daemon_mode);
						if(connection_remove(epfd, fd, ctable)){
							assert(connection_get(fd, ctable) == NULL);
							close(fd);
		#ifdef DEBUG_BUILD
							int num_connections = connection_count(ctable);
							if (num_connections == 0) {
								db_check_table_refs();
							}
		#endif
						} else {
							WARN("Unable to remove connection %d (not found in table)", fd);
						}
					}
				}
				else
				{
					WARN("Unknown connection %d", fd);
					assert(fd != 0 || settings.daemon_mode);
					close(fd);
				}
				
			}
			n++;
		}
	}

	close(epfd);
	
	pthread_join(tid, NULL);
	
	close(efd);
	free(thread_arg);
}

/*
On close connection cleanup routine
*/
void connection_cleanup_http(cache_connection_node* connection, bool toFree = false) {
	assert(connection != NULL);

	//Close socket to client
	if (connection->connection.client_sock != -1) {
		http_cleanup(&connection->connection);
		close(connection->connection.client_sock);
		connection->connection.client_sock = -1;
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
	if (listeners.fds != NULL) {
		connection_close_listener();
	}

	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++) {
		connection_cleanup_http(&ctable[i]);
	}
}