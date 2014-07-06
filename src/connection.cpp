#include "connection.h"
#include "debug.h"

/* Globals */
int listenfd;
struct epoll_event ev;

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

/*----------------------------------------------------------------------
Portable function to set a socket into nonblocking mode.
Calling this on a socket causes all future read() and write() calls on
that socket to do only as much as they can immediately, and return
without waiting.
If no data can be read or written, they return -1 and set errno
to EAGAIN (or EWOULDBLOCK).
Thanks to Bjorn Reese for this code.
----------------------------------------------------------------------*/
int setNonblocking(int fd)
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

void open_listener(){
	int res;

	struct sockaddr_in servaddr;
	/* Set up to be a daemon listening on port 8000 */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);


	/* Enable address reuse */
	int on = 1;
	res = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (res < 0){
		goto fail;
	}

	//bind
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(8000);
	res = bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
	if (res < 0){
		goto fail;
	}
	/* Force the network socket into nonblocking mode */
	//res = setNonblocking(listenfd);
	//if (res < 0){
	//		goto fail;
	//}


	res = listen(listenfd, 10);
	if (res < 0){
		goto fail;
	}

	return;
fail:
	PFATAL("error opening listener");
}

static void connection_add(int fd, cache_connection_node* ctable){
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock != -1){
		while (node->next != NULL) {
			node = node->next;
		}

		cache_connection_node* newNode = (cache_connection_node*)malloc(sizeof(cache_connection_node));
		node->next = newNode;
		node = newNode;
	}
	node->connection.input_read_position = 0;
	node->connection.input_buffer_write_position = 0;
	node->connection.state = STATE_READING_REQUEST_METHOD;
	node->connection.client_sock = fd;
	node->connection.output_buffer_free = NULL;
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