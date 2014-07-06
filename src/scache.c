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
#include "config.h"
#include "debug.h"
#include "scache.h"
#include "hash.h"
#include "http.h"
#include "db.h"

#define IS_SINGLE_FILE(x) x->data_length>BLOCK_LENGTH
#define CONNECTION_HASH_KEY(x) x%CONNECTION_HASH_ENTRIES

//Misc
int stop_soon = 0;
struct epoll_event ev;

static void epoll_event_loop(void){
	int epfd = epoll_create(MAXCLIENTS);

	struct epoll_event events[5];
	//add api fd
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
	ev.data.fd = listenfd;
	int res = epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);
	if (res != 0){
		PFATAL("epoll_ctl() failed.");
	}

	struct cache_connection_node ctable[CONNECTION_HASH_ENTRIES] = { 0 };
	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++){
		ctable[i].connection.client_sock = -1;
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

						if (fcntl(client_sock, F_SETFL, O_NONBLOCK))
							PFATAL("fcntl() to set O_NONBLOCK on API connection fails.");

						ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
						ev.data.fd = client_sock;
						res = epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &ev);
						if (res != 0){
							PFATAL("epoll_ctl() failed.");
						}

						connection_add(client_sock, ctable);
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
						if (handle_fd_read(fd, epfd, connection)){
							close_connection = 1;
						}
					}
					else if (events[n].events & EPOLLOUT){
						if (handle_fd(fd, epfd, connection)){
							close_connection = 1;
						}
					}

					if (close_connection){
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

/* Time to go down the rabbit hole */
int main()
{
	http_templates_init();
	db_open("/dbtest");
	open_listener();
	epoll_event_loop();
}