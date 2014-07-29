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
#include <fcntl.h>
#include <errno.h>
#include "connection.h"
#include "debug.h"
#include "http.h"
#include "settings.h"

/* Globals */
int listenfd = -1;
struct epoll_event ev;
struct cache_connection_node ctable[CONNECTION_HASH_ENTRIES] = { 0 };

//Misc
int stop_soon = 0;

/* Methods */
void connection_register_write(int epfd, int fd){
	ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
	ev.data.fd = fd;
	int res = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	if (res != 0){
		PFATAL("epoll_ctl() failed.");
	}
}

void connection_register_read(int epfd, int fd){
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
	ev.data.fd = fd;
	int res = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	if (res != 0){
		PFATAL("epoll_ctl() failed.");
	}
}

void connection_setup(){
	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++){
		ctable[i].connection.client_sock = -1;
	}
	connection_open_listener();
}

/*----------------------------------------------------------------------
Portable function to set a socket into nonblocking mode.
Calling this on a socket causes all future read() and write() calls on
that socket to do only as much as they can immediately, and return
without waiting.
If no data can be read or written, they return -1 and set errno
to EAGAIN (or EWOULDBLOCK).
Thanks to Bjorn Reese for this code.
----------------------------------------------------------------------*/
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
	close(listenfd);
	listenfd = -1;
}

void connection_open_listener(){
	int res;

	struct sockaddr_in servaddr;
	/* Set up to be a daemon listening on port 8000 */
	listenfd = socket(settings.bind_af, SOCK_STREAM, 0);


	/* Enable address reuse */
	int on = 1;
	res = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (res < 0){
		goto fail;
	}

	//bind1
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = settings.bind_af;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(settings.bind_port);
	res = bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
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

	SAYF("Listening on %d\n", settings.bind_port);

	return;
fail:
	PFATAL("error opening listener (:%d)", settings.bind_port);
}

static cache_connection* connection_add(int fd, cache_connection_node* ctable){
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock != -1){
		while (node->next != NULL) {
			node = node->next;
		}

		cache_connection_node* newNode = (cache_connection_node*)malloc(sizeof(cache_connection_node));
		node->next = newNode;
		newNode->next = NULL;
		node = newNode;
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
	while (node->connection.client_sock != fd){
		node = node->next;
		if (node == NULL){
			return NULL;
		}
	}

	return &node->connection;
}

static void connection_remove(int fd, cache_connection_node* ctable){
	cache_connection_node* prev = NULL;
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	while (node->connection.client_sock != fd){
		prev = node;
		node = node->next;
		if (node == NULL){
			WARN("Unable to find fd: %d connection entry to remove", fd);
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
			//Has nodes after it
			memcpy(&node->next->connection, &node->connection, sizeof(cache_connection));
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


void connection_event_loop(void (*connection_handler)(cache_connection* connection)){
	int epfd = epoll_create(MAXCLIENTS);

	struct epoll_event events[5];
	//add api fd
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
	ev.data.fd = listenfd;
	int res = epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);
	if (res != 0){
		PFATAL("epoll_ctl() failed.");
	}

	while (!stop_soon) {
		int nfds = epoll_wait(epfd, events, 5, -1);
		int n = 0;
		while (n < nfds) {
			int fd = events[n].data.fd;
			if (fd == listenfd){
				if (events[n].events & EPOLLIN){
					DEBUG("[#] Accepting connection\n");
					int client_sock = accept(listenfd, NULL, NULL);

					if (client_sock < 0) {
						WARN("Unable to handle API connection: accept() fails. Error: %s", strerror(errno));
					}
					else {
						DEBUG("[#] Accepted connection %d\n", client_sock);

						if (connection_non_blocking(client_sock) < 0)
							PFATAL("Setting connection to non blocking failed.");

						ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
						ev.data.fd = client_sock;
						res = epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &ev);
						if (res != 0){
							PFATAL("epoll_ctl() failed.");
						}

						cache_connection* connection = connection_add(client_sock, ctable);
						connection_handler(connection);
					}
				}
				else if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP){
					FATAL("API socket is down.");
				}
			}
			else{
				DEBUG("[#%d] Got socket event %d\n", fd, events[n].events);
				cache_connection* connection = connection_get(fd, ctable);
				if (connection != NULL){
					int close_connection = 0;

					if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP || events[n].events & EPOLLRDHUP){
						close_connection = 1;
					}
					else if (events[n].events & EPOLLIN){
						if (http_read_handle(epfd, connection) == close_connection){
							close_connection = 1;
						}
					}
					else if (events[n].events & EPOLLOUT){
						if (http_write_handle(epfd, connection) == close_connection){
							close_connection = 1;
						}
					}


					if (close_connection){
						http_cleanup(connection);
						connection_remove(fd, ctable);
						close(fd);
					}
				}
				else{
					WARN("Unknown connection %d", fd);
					close(fd);
				}
			}
			n++;
		}
	}

	close(epfd);
}

void connection_cleanup_http(cache_connection_node* connection, bool toFree = false){
	http_cleanup(&connection->connection);
	close(connection->connection.client_sock);
	if (connection->next != NULL){
		connection_cleanup_http(connection->next, true);
	}
	if (toFree){
		free(connection);
	}
}

void connection_cleanup(){
	if (listenfd != -1){
		connection_close_listener();
	}

	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++){
		connection_cleanup_http(&ctable[i]);
	}
}